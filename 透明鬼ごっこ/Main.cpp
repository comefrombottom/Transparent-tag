# include <Siv3D.hpp> // Siv3D v0.6.14
# include "Multiplayer_Photon.hpp"
# include "PHOTON_APP_ID.SECRET"


InputGroup KeyGroupLeft{ KeyLeft, KeyA };
InputGroup KeyGroupRight{ KeyRight, KeyD };
InputGroup KeyGroupUp{ KeyUp, KeyW };
InputGroup KeyGroupDown{ KeyDown, KeyS };

enum class NetWorkState {
	Disconnected,
	Connecting,
	InLobby,
	Joining,
	InRoom,
	Leaving,
	Disconnecting,
};

[[nodiscard]]
StringView ToString(NetWorkState state) {
	switch (state) {
	case NetWorkState::Disconnected:
		return U"Disconnected";
	case NetWorkState::Connecting:
		return U"Connecting";
	case NetWorkState::InLobby:
		return U"InLobby";
	case NetWorkState::Joining:
		return U"Joining";
	case NetWorkState::InRoom:
		return U"InRoom";
	case NetWorkState::Leaving:
		return U"Leaving";
	case NetWorkState::Disconnecting:
		return U"Disconnecting";
	default:
		return U"Unknown";
	}
}

void drawCrown(const Vec2& pos) {
	constexpr double w = 20;
	constexpr double h = 15;
	constexpr Vec2 bottom_left = Vec2{ -w/2,0 };
	constexpr Vec2 left = Vec2{ -w/2,-h };
	constexpr Vec2 leftV = Vec2{ -w / 3,-h / 2 };
	constexpr Vec2 top = Vec2{ 0,-h };
	constexpr Vec2 rightV = Vec2{ w / 3,-h / 2 };
	constexpr Vec2 right = Vec2{ w/2,-h };
	constexpr Vec2 bottom_right =  Vec2{ w/2,0 };
	Transformer2D tf{ Mat3x2::Translate(pos) };
	Polygon{ bottom_left,left,leftV,top,rightV,right,bottom_right }.draw(Palette::Yellow);
}

struct Player {
	Vec2 pos;
	bool isTransparent = false;

	bool isWatching = false;
	ColorF color;
	String name;

	Player() = default;
	Player(const Vec2& pos,const Color& color,const String& name) : pos(pos),color(color),name(name),isTransparent(false),isWatching(false) {}

	template <class Archive>
	void SIV3D_SERIALIZE(Archive& archive)
	{
		archive(pos, isTransparent, isWatching, color, name);
	}
};

class ShareRoomData {

public:
	ShareRoomData() = default;

	const HashTable<LocalPlayerID, Player>& players() const {
		return m_players;
	}

	const LocalPlayerID itID() const {
		return m_itID;
	}

	void setPlayerPos(LocalPlayerID id, Vec2 pos) {
		m_players.at(id).pos = pos;
	}

	void addPlayer(LocalPlayerID id, Vec2 pos, Color color, String name) {
		m_players.insert_or_assign(id, Player(pos, color, name));
	}

	void erasePlayer(LocalPlayerID id) {
		m_players.erase(id);
	}

	void beTransparent(LocalPlayerID id, bool beTransparent) {
		m_players.at(id).isTransparent = beTransparent;
	}

	void beWatching(LocalPlayerID id, bool beWatching) {
		m_players.at(id).isWatching = beWatching;
	}

	void setPlayerName(LocalPlayerID id, const String& name) {
		m_players.at(id).name = name;
	}

	void setItID(LocalPlayerID id) {
		m_itID = id;
	}

	template <class Archive>
	void SIV3D_SERIALIZE(Archive& archive)
	{
		archive(m_players,m_itID);
	}
private:
	HashTable<LocalPlayerID,Player> m_players;
	LocalPlayerID m_itID = 0;
};

enum class EventCode:uint8 {
	roomDataFromHost,
	playerAdd,
	playerErase,
	playerMove,
	beTransparent,
	beWatching,
	playerNameChange,
	itIDChange,
	tagStop,
};

class MyNetwork : public Multiplayer_Photon
{
public:
	using Multiplayer_Photon::Multiplayer_Photon;

	NetWorkState state = NetWorkState::Disconnected;
	bool isFirstConnecting = true;

	//Lobby Data
	TextEditState roomNameBox;
	TextEditState userNameBox;
	void initWhenEnterLobby() {
		roomNameBox = TextEditState{ U"room" };
		userNameBox = TextEditState{ U"player" };
	}
	void updateLobby() {

		SimpleGUI::TextBox(roomNameBox, Vec2{ 100,20 }, 200);
		if (SimpleGUI::Button(U"ルームを作成", Vec2{ 100,70 })) {
			initWhenCreateRoom();
			createRoom(roomNameBox.text + U"#" + ToHex(RandomUint16()), 20);
			state = NetWorkState::Joining;
		}

		SimpleGUI::TextBox(userNameBox, Vec2{ 580,20 }, 200);

		for(auto [i,roomName] : Indexed(getRoomNameList())){
			if (SimpleGUI::Button(roomName, Vec2{ 100 + (i % 3) * 200,150 + (i / 3) * 50 })) {
				initWhenJoinRoom();
				joinRoom(roomName);
				state = NetWorkState::Joining;
			}
		}
	}
	

	//Room Data
	static constexpr double playerRadius = 15;
	static constexpr double observerSearchRadius = 30;
	static constexpr double observerBodyRadius = 7;

	bool hasRoomData = false;
	ShareRoomData roomData;
	P2World world;
	static constexpr double stepTime = 1.0 / 200.0;
	double accumulatedTime = 0.0;
	Array<P2Body> walls;
	P2Body playerBody;
	Timer tagStoppingTimer;
	Stopwatch noMovingTime;
	double observerAccumulatedTime = 0.0;
	constexpr static double observerStepTime = 10;
	struct Observer {
		Vec2 pos;
		bool isWatching = false;
	};
	Array<Observer> observers;

	struct PlayerLocalData {
		PlayerLocalData() = default;
		PlayerLocalData(const Vec2& pos) : pos(pos) {}
		Vec2 pos;
		Vec2 velocity{};
		Timer fadeoutTimer;
		bool isFacingRight = true;
	};

	HashTable<LocalPlayerID, PlayerLocalData> playersLocalData;

	void flipFadeoutTimer(const LocalPlayerID playerID) {
		constexpr Duration fadeoutTime = 0.1s;
		Timer& fadeoutTimer = playersLocalData.at(playerID).fadeoutTimer;
		Duration remain = fadeoutTimer.remaining();
		fadeoutTimer.restart(fadeoutTime);
		fadeoutTimer.setRemaining(fadeoutTime - remain);
	}

	const Player& getPlayer() const {
		return roomData.players().at(getLocalPlayerID());
	}

	bool isIt() const {
		return getLocalPlayerID() == roomData.itID();
	}
	
	void createWalls() {
		world = P2World{ {0, 0} };

		walls << world.createRect(P2Static, Scene::Rect().topCenter(), Size{ Scene::Width(),100 });
		walls << world.createRect(P2Static, Scene::Rect().bottomCenter(), Size{ Scene::Width(),100 });
		walls << world.createRect(P2Static, Scene::Rect().leftCenter(), Size{ 100,Scene::Height() });
		walls << world.createRect(P2Static, Scene::Rect().rightCenter(), Size{ 100,Scene::Height() });

		walls << world.createRect(P2Static, Vec2{ 300,300 }, Size{ 100,100 });
		walls << world.createRect(P2Static, Vec2{ 500,300 }, Size{ 100,100 });
		walls << world.createRect(P2Static, Vec2{ 400,400 }, Size{ 100,100 });
	}

	void initRoomData() {
		hasRoomData = false;
		roomData = ShareRoomData{};
		createWalls();
		playerBody = world.createCircle(P2Dynamic, { 400,300 }, 15).setFixedRotation(true);
		accumulatedTime = 0.0;
		observerAccumulatedTime = 0.0;
		observers.clear();
		playersLocalData.clear();
	}

	void initWhenCreateRoom() {
		initRoomData();
		hasRoomData = true;
	}

	void initWhenJoinRoom() {
		initRoomData();
		hasRoomData = false;
	}

	void updateRoom(double delta = Scene::DeltaTime()) {
		
		if(not hasRoomData)return;
		//Vec2 inputAxis = Vec2(KeyGroupRight.pressed() - KeyGroupLeft.pressed(), KeyGroupDown.pressed() - KeyGroupUp.pressed());
		Vec2 inputAxis = Vec2(KeyD.pressed() - KeyA.pressed(), KeyS.pressed() - KeyW.pressed());
		bool beTransparent = KeySpace.pressed();
		Vec2 prePos = playerBody.getPos();
		Vec2 normalizedInputAxis = inputAxis.setLength(1);

		double speed = beTransparent ? 120 : 200;

		if(tagStoppingTimer.isRunning() and isIt()){
			speed = 0;
		}

		playerBody.setVelocity(normalizedInputAxis * speed);

		for (accumulatedTime += delta; accumulatedTime >= stepTime; accumulatedTime -= stepTime) {
			world.update(stepTime);
		}

		Vec2 pos = playerBody.getPos();
		if (prePos != pos) {
			roomData.setPlayerPos(getLocalPlayerID(), pos);
			sendEvent(FromEnum(EventCode::playerMove), Serializer<MemoryWriter>{}(pos));
			noMovingTime.restart();
			roomData.beWatching(getLocalPlayerID(), false);
			sendEvent(FromEnum(EventCode::beWatching), Serializer<MemoryWriter>{}(false));
		}
		if(beTransparent){
			noMovingTime.restart();
			roomData.beWatching(getLocalPlayerID(), false);
			sendEvent(FromEnum(EventCode::beWatching), Serializer<MemoryWriter>{}(false));
		}

		if (beTransparent != getPlayer().isTransparent) {
			roomData.beTransparent(getLocalPlayerID(), beTransparent);
			flipFadeoutTimer(getLocalPlayerID());
			sendEvent(FromEnum(EventCode::beTransparent), Serializer<MemoryWriter>{}(beTransparent));
		}

		if(noMovingTime > 1.0s){
			roomData.beWatching(getLocalPlayerID(), true);
			sendEvent(FromEnum(EventCode::beWatching), Serializer<MemoryWriter>{}(true));
		}

		for (auto& [id, player] : roomData.players()) {
			if (playersLocalData.contains(id)) {
				Vec2& velocity = playersLocalData.at(id).velocity;
				playersLocalData.at(id).pos = Math::SmoothDamp(playersLocalData.at(id).pos, player.pos, velocity, 1.0 / 20, unspecified, delta);
				bool& isFacingRight = playersLocalData.at(id).isFacingRight;
				if (velocity.x > 0.1) {
					isFacingRight = true;
				}
				else if (velocity.x < -0.1) {
					isFacingRight = false;
				}
			}
			else {
				playersLocalData.insert_or_assign(id, PlayerLocalData{ player.pos });
			}
		}


		if (not tagStoppingTimer.isRunning() and isIt()) {
			for (auto& [id, player] : roomData.players()) {
				if (id == getLocalPlayerID())continue;

				if (playerBody.getPos().asCircle(playerRadius).intersects(playersLocalData.at(id).pos.asCircle(playerRadius))) {
					roomData.setItID(id);
					sendEvent(FromEnum(EventCode::itIDChange), Serializer<MemoryWriter>{}(id));

					tagStoppingTimer.restart(3.0s);
					observers.clear();
					observerAccumulatedTime = 0.0;
					sendEvent(FromEnum(EventCode::tagStop), Serializer<MemoryWriter>{});
				}
			}
		}

		for (observerAccumulatedTime += delta; observerAccumulatedTime >= observerStepTime; observerAccumulatedTime -= observerStepTime) {
			observers.push_back(Observer{ playerBody.getPos() });
		}

		for (auto& observer : observers) {
			observer.isWatching = false;
			for (auto& [id, player] : roomData.players()) {
				if (id == getLocalPlayerID())continue;

				if (observer.pos.asCircle(observerSearchRadius).intersects(player.pos.asCircle(playerRadius))) {
					observer.isWatching = true;
				}
			}
		}

	}

	void drawRoom() {

		if (not hasRoomData) {
			FontAsset(U"message")(U"データを受信中...").drawAt(Scene::Center(), Palette::White);
			return;
		}

		for (auto& observer : observers) {
			if (observer.isWatching) {
				observer.pos.asCircle(observerSearchRadius).draw(ColorF(1, 0.3));
				observer.pos.asCircle(observerBodyRadius).draw(Palette::Red);
			}
			else {
				observer.pos.asCircle(observerBodyRadius).draw(Palette::Blue);
			}
		}

		for (auto& wall : walls) {
			wall.draw();
		}

		for (auto& [id, player] : roomData.players()) {

			if (id == getLocalPlayerID())continue;

			double alpha = 1.0;
			if (player.isTransparent) {
				alpha = playersLocalData.at(id).fadeoutTimer.progress1_0();
				if(noMovingTime > 1.0s){
					alpha = Min((noMovingTime.sF() - 1.0), 0.5);
				}
			}
			else {
				alpha = playersLocalData.at(id).fadeoutTimer.progress0_1();
			}
			const Vec2& pos = playersLocalData.at(id).pos;
			ScopedColorMul2D scm{ 1.0,1.0,1.0,alpha };
			if (id == roomData.itID())drawCrown(pos + Vec2(0, -20));
			pos.asCircle(playerRadius).draw(player.color);
			if (player.isWatching) {
				pos.asCircle(playerRadius * 2 / 3).draw(Palette::White);
				pos.asCircle(playerRadius * 1 / 3).draw(Palette::Black);
			}

			if(playersLocalData.at(id).isFacingRight){
				Line{ pos ,pos + Vec2{playerRadius,0} }.draw(2,Palette::White);
			}
			else {
				Line{ pos,pos + Vec2{-playerRadius,0} }.draw(2,Palette::White);
			}

			FontAsset(U"name")(player.name).drawAt(pos + Vec2{ 0,-20 }, ColorF(1));
		}

		const Player& player = getPlayer();
		LocalPlayerID id = getLocalPlayerID();
		double alpha = 1.0;
		if (player.isTransparent) {
			alpha = (playersLocalData.at(id).fadeoutTimer.progress1_0() * 0.75 + 0.25);
		}
		else {
			alpha = (playersLocalData.at(id).fadeoutTimer.progress0_1() * 0.75 + 0.25);
		}
		const Vec2& pos = playerBody.getPos();
		ScopedColorMul2D scm{ 1.0,1.0,1.0,alpha };
		if (id == roomData.itID())drawCrown(pos + Vec2(0, -20));
		playerBody.draw(player.color);
		if (player.isWatching) {
			pos.asCircle(playerRadius * 2 / 3).draw(Palette::White);
			pos.asCircle(playerRadius * 1 / 3).draw(Palette::Black);
		}
		
		if (playersLocalData.at(id).isFacingRight) {
			Line{ pos,pos + Vec2{playerRadius,0} }.draw(2,Palette::White);
		}
		else {
			Line{ pos,pos + Vec2{-playerRadius,0} }.draw(2,Palette::White);
		}

		FontAsset(U"name")(player.name).drawAt(pos + Vec2{ 0,-20 }, ColorF(1.0));
		
		Print << U"TagStoppingTimer:" << tagStoppingTimer.remaining();

		//observerAccumulatedTime Circle
		Circle{ Scene::Size() - Vec2{40,40},25 }.drawPie(0, observerAccumulatedTime / observerStepTime * Math::TwoPi, Palette::Blue).drawFrame(3,Palette::Gray);


		if (SimpleGUI::Button(U"Exit", Vec2{ 700,10 })) {
			leaveRoom();
			state = NetWorkState::Leaving;
		}
	}

	void connectReturn([[maybe_unused]] const int32 errorCode, const String& errorString, const String& region, [[maybe_unused]] const String& cluster) override
	{
		state = NetWorkState::InLobby;
		isFirstConnecting = false;
	}

	void disconnectReturn() override
	{
		state = NetWorkState::Disconnected;
	}

	void leaveRoomReturn(int32 errorCode, const String& errorString) override
	{
		state = NetWorkState::InLobby;
	}

	void joinRoomEventAction(const LocalPlayer& newPlayer, [[maybe_unused]] const Array<LocalPlayerID>& playerIDs, const bool isSelf) override {
		if (isSelf) {
			state = NetWorkState::InRoom;

			noMovingTime.restart();
			if (isHost()) {

				roomData.addPlayer(newPlayer.localID, Vec2{ 400,300 }, RandomColor(), userNameBox.text);
				playersLocalData.insert_or_assign(newPlayer.localID, PlayerLocalData{ Vec2{ 400,300 } });

				roomData.setItID(newPlayer.localID);
			}
			else {
			}
		}
		else {
			if (isHost()) {
				sendEvent(FromEnum(EventCode::roomDataFromHost), Serializer<MemoryWriter>{}(roomData, observerAccumulatedTime), Array{ newPlayer.localID });
			}
		}
	}

	void leaveRoomEventAction(const LocalPlayerID playerID, [[maybe_unused]] const bool isInactive) override {
		if (isHost()) {
			roomData.erasePlayer(playerID);
			sendEvent(FromEnum(EventCode::playerErase), Serializer<MemoryWriter>{}(playerID));
		}
	}

	void customEventAction(const LocalPlayerID playerID, const uint8 eventCode, Deserializer<MemoryViewReader>& reader) override
	{
		auto eventCodeEnum = ToEnum<EventCode>(eventCode);
		switch (eventCodeEnum) {
		case EventCode::roomDataFromHost:
		{
			assert(not hasRoomData);
			reader(roomData, observerAccumulatedTime);
			hasRoomData = true;
			Vec2 pos = Vec2{ 400,300 };
			Color color = RandomColor();
			roomData.addPlayer(getLocalPlayerID(), pos, color, userNameBox.text);
			playersLocalData.insert_or_assign(getLocalPlayerID(), PlayerLocalData{ pos });
			sendEvent(FromEnum(EventCode::playerAdd), Serializer<MemoryWriter>{}(pos, color, userNameBox.text));
		}
			break;
		case EventCode::playerAdd:
		{
			if (not hasRoomData) return;
			Vec2 pos;
			Color color;
			String name;
			reader(pos, color, name);
			roomData.addPlayer(playerID, pos, color, name);
			playersLocalData.insert_or_assign(playerID, PlayerLocalData{ pos });
		}
			break;
		case EventCode::playerErase:
			if (not hasRoomData) return;
			roomData.erasePlayer(playerID);
			break;
		case EventCode::playerMove:
		{
			if (not hasRoomData) return;
			Vec2 pos;
			reader(pos);
			roomData.setPlayerPos(playerID, pos);
		}
			break;
		case EventCode::beTransparent:
		{
			if (not hasRoomData) return;
			bool beTransparent;
			reader(beTransparent);
			roomData.beTransparent(playerID, beTransparent);
			flipFadeoutTimer(playerID);
		}
			break;
		case EventCode::beWatching:
		{
			if (not hasRoomData) return;
			bool beWatching;
			reader(beWatching);
			roomData.beWatching(playerID, beWatching);
		}
			break;
		case EventCode::playerNameChange:
		{
			if (not hasRoomData) return;
			String name;
			reader(name);
			roomData.setPlayerName(playerID, name);
		}
			break;
		case EventCode::itIDChange:
		{
			if (not hasRoomData) return;
			LocalPlayerID itID;
			reader(itID);
			roomData.setItID(itID);
		}
			break;
		case EventCode::tagStop:
		{
			if (not hasRoomData) return;
			tagStoppingTimer.restart(3.0s);
			observers.clear();
			observerAccumulatedTime = 0.0;
		}
			break;
		default:
			break;
		}

	}
};


void Main()
{
	FontAsset::Register(U"message", 30, Typeface::Bold);
	FontAsset::Register(U"name", 15, Typeface::Bold);


	const std::string secretAppID{ SIV3D_OBFUSCATE(PHOTON_APP_ID) };

	MyNetwork network{ secretAppID, U"1.0", Verbose::No };

	while (System::Update())
	{
		ClearPrint();

		if(not network.isActive()){
			network.initWhenEnterLobby();
			network.connect(U"player", U"jp");
			network.state = NetWorkState::Connecting;
		}
		else {
			network.update();
		}

		switch (network.state)
		{
		case NetWorkState::Disconnected:
			break;
		case NetWorkState::Connecting:
			if (network.isFirstConnecting) {
				FontAsset(U"message")(U"接続しています...").drawAt(Scene::Center(), Palette::White);
			}
			else {
				FontAsset(U"message")(U"接続が切れました\n再接続しています...").drawAt(Scene::Center(), Palette::White);
			}
			break;
		case NetWorkState::InLobby:
			network.updateLobby();
			break;
		case NetWorkState::Joining:
			FontAsset(U"message")(U"入室中...").drawAt(Scene::Center(), Palette::White);
			break;
		case NetWorkState::InRoom:
			network.updateRoom();
			network.drawRoom();
			break;
		case NetWorkState::Leaving:
			FontAsset(U"message")(U"退室中...").drawAt(Scene::Center(), Palette::White);
			break;
		case NetWorkState::Disconnecting:
			FontAsset(U"message")(U"切断中...").drawAt(Scene::Center(), Palette::White);
			break;
		default:
			break;
		}

	}
}

//
// - Debug ビルド: プログラムの最適化を減らす代わりに、エラーやクラッシュ時に詳細な情報を得られます。
//
// - Release ビルド: 最大限の最適化でビルドします。
//
// - [デバッグ] メニュー → [デバッグの開始] でプログラムを実行すると、[出力] ウィンドウに詳細なログが表示され、エラーの原因を探せます。
//
// - Visual Studio を更新した直後は、プログラムのリビルド（[ビルド]メニュー → [ソリューションのリビルド]）が必要な場合があります。
//
