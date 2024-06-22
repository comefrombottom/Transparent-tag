# include <Siv3D.hpp> // Siv3D v0.6.14
# include "Multiplayer_Photon.hpp"
# include "PHOTON_APP_ID.SECRET"


InputGroup KeyGroupLeft{ KeyLeft, KeyA };
InputGroup KeyGroupRight{ KeyRight, KeyD };
InputGroup KeyGroupUp{ KeyUp, KeyW };
InputGroup KeyGroupDown{ KeyDown, KeyS };

enum class ConnectingOrJoining {
	Connecting,
	Joining
};

void drawCrown(const Vec2& pos) {
	constexpr double w = 20;
	constexpr double h = 15;
	const Vec2 bottom_left = Vec2{ -w/2,0 };
	const Vec2 left = Vec2{ -w/2,-h };
	const Vec2 leftV = Vec2{ -w / 3,-h / 2 };
	const Vec2 top = Vec2{ 0,-h };
	const Vec2 rightV = Vec2{ w / 3,-h / 2 };
	const Vec2 right = Vec2{ w/2,-h };
	const Vec2 bottom_right =  Vec2{ w/2,0 };
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

	const Array<Vec2>& traps() const {
		return m_traps;
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

	void setPlayerName(LocalPlayerID id, const String& name) {
		m_players.at(id).name = name;
	}

	void setItID(LocalPlayerID id) {
		m_itID = id;
	}

	template <class Archive>
	void SIV3D_SERIALIZE(Archive& archive)
	{
		archive(m_players,m_traps,m_itID);
	}
private:
	HashTable<LocalPlayerID,Player> m_players;
	Array<Vec2> m_traps;
	LocalPlayerID m_itID = 0;
};

enum class EventCode:uint8 {
	roomDataFromHost,
	playerAdd,
	playerErase,
	playerMove,
	beTransparent,
	playerNameChange,
	itIDChange,
	tagStop,
};

class MyNetwork : public Multiplayer_Photon
{
public:
	using Multiplayer_Photon::Multiplayer_Photon;

	ConnectingOrJoining connectingOrJoining = ConnectingOrJoining::Connecting;

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
			connectingOrJoining = ConnectingOrJoining::Joining;
		}

		SimpleGUI::TextBox(userNameBox, Vec2{ 580,20 }, 200);

		for(auto [i,roomName] : Indexed(getRoomNameList())){
			if (SimpleGUI::Button(roomName, Vec2{ 100 + (i % 3) * 200,150 + (i / 3) * 50 })) {
				initWhenJoinRoom();
				joinRoom(roomName);
				connectingOrJoining = ConnectingOrJoining::Joining;
			}
		}
	}
	

	//Room Data
	bool hasRoomData = false;
	ShareRoomData roomData;
	P2World world;
	static constexpr double stepTime = 1.0 / 200.0;
	double accumulatedTime = 0.0;
	Array<P2Body> walls;
	P2Body playerBody;
	Timer tagStoppingTimer;
	Stopwatch noMovingTime;

	struct PlayerLocalData {
		PlayerLocalData() = default;
		PlayerLocalData(const Vec2& pos) : pos(pos) {}
		Vec2 pos;
		Vec2 velocity{};
		Timer fadeoutTimer;
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

	void initWhenCreateRoom() {
		hasRoomData = true;
		createWalls();
		playerBody = world.createCircle(P2Dynamic, { 400,300 }, 15).setFixedRotation(true);
	}

	void initWhenJoinRoom() {
		hasRoomData = false;
		createWalls();
		playerBody = world.createCircle(P2Dynamic, { 400,300 }, 15).setFixedRotation(true);
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
		}
		if(beTransparent){
			noMovingTime.restart();
		}

		if (beTransparent != getPlayer().isTransparent) {
			roomData.beTransparent(getLocalPlayerID(), beTransparent);
			flipFadeoutTimer(getLocalPlayerID());
			sendEvent(FromEnum(EventCode::beTransparent), Serializer<MemoryWriter>{}(beTransparent));
		}

		for (auto& [id, player] : roomData.players()) {
			if (id == getLocalPlayerID())continue;

			if (playersLocalData.contains(id)) {
				playersLocalData.at(id).pos = Math::SmoothDamp(playersLocalData.at(id).pos, player.pos, playersLocalData.at(id).velocity, 1.0/20, unspecified, delta);
			}
			else {
				playersLocalData.insert_or_assign(id, PlayerLocalData{ player.pos });
			}
		}


		if (not tagStoppingTimer.isRunning() and isIt()) {
			for (auto& [id, player] : roomData.players()) {
				if (id == getLocalPlayerID())continue;

				if (playerBody.getPos().asCircle(15).intersects(playersLocalData.at(id).pos.asCircle(15))) {
					roomData.setItID(id);
					sendEvent(FromEnum(EventCode::itIDChange), Serializer<MemoryWriter>{}(id));

					tagStoppingTimer.restart(3.0s);
					sendEvent(FromEnum(EventCode::tagStop), Serializer<MemoryWriter>{});
				}
			}
		}

	}

	void drawRoom() {

		if (not hasRoomData) {
			FontAsset(U"message")(U"データを受信中...").drawAt(Scene::Center(), Palette::White);
			return;
		}

		for (auto& wall : walls) {
			wall.draw();
		}

		for (auto& [id, player] : roomData.players()) {

			if (id == getLocalPlayerID())continue;

			if (player.isTransparent) {
				const Vec2& pos = playersLocalData.at(id).pos;
				double fadeoutAlpha = playersLocalData.at(id).fadeoutTimer.progress1_0();
				ScopedColorMul2D scm{ 1.0,1.0,1.0,fadeoutAlpha };

				if (fadeoutAlpha > 0.0) {
					if (id == roomData.itID())drawCrown(pos + Vec2(0, -20));
					pos.asCircle(15).draw(player.color);
					FontAsset(U"name")(player.name).drawAt(pos + Vec2{ 0,-20 }, ColorF(1));
				}

				if(noMovingTime > 1.0s){
					double alpha = Min((noMovingTime.sF() - 1.0), 0.5);
					ScopedColorMul2D scm{ 1.0,1.0,1.0,alpha };
					if (id == roomData.itID())drawCrown(pos + Vec2(0, -20));
					pos.asCircle(15).draw(player.color);
					FontAsset(U"name")(player.name).drawAt(pos + Vec2{ 0,-20 }, ColorF(1));
				}
			}
			else {
				const Vec2& pos = playersLocalData.at(id).pos;
				double fadeoutAlpha = playersLocalData.at(id).fadeoutTimer.progress0_1();
				ScopedColorMul2D scm{ 1.0,1.0,1.0,fadeoutAlpha };
				if (id == roomData.itID())drawCrown(pos + Vec2(0, -20));
				pos.asCircle(15).draw(player.color);
				FontAsset(U"name")(player.name).drawAt(pos + Vec2{ 0,-20 }, ColorF(1));
			}
		}

		if (getPlayer().isTransparent) {
			double fadeoutAlpha = (playersLocalData.at(getLocalPlayerID()).fadeoutTimer.progress1_0() * 0.75 + 0.25);
			ScopedColorMul2D scm{ 1.0,1.0,1.0,fadeoutAlpha };
			if (getLocalPlayerID() == roomData.itID())drawCrown(playerBody.getPos() + Vec2(0,-20));
			playerBody.draw(getPlayer().color);
			FontAsset(U"name")(getPlayer().name).drawAt(playerBody.getPos() + Vec2{ 0,-20 }, ColorF(1.0));
		}
		else {
			double fadeoutAlpha = (playersLocalData.at(getLocalPlayerID()).fadeoutTimer.progress0_1() * 0.75 + 0.25);
			ScopedColorMul2D scm{ 1.0,1.0,1.0,fadeoutAlpha };
			if (getLocalPlayerID() == roomData.itID())drawCrown(playerBody.getPos() + Vec2(0, -20));
			playerBody.draw(getPlayer().color);
			if (noMovingTime > 1s) {
				Vec2 pos = playerBody.getPos();
				pos.asCircle(10).draw(Palette::White);
				pos.asCircle(5).draw(Palette::Black);

			}
			FontAsset(U"name")(getPlayer().name).drawAt(playerBody.getPos() + Vec2{ 0,-20 }, ColorF(1.0));
		}
		Print << U"TagStoppingTimer:" << tagStoppingTimer.remaining();

		if (SimpleGUI::Button(U"Exit", Vec2{ 700,10 })) {
			leaveRoom();
		}
	}

	void joinRoomEventAction(const LocalPlayer& newPlayer, [[maybe_unused]] const Array<LocalPlayerID>& playerIDs, const bool isSelf) override {
		if (isSelf) {
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
				sendEvent(FromEnum(EventCode::roomDataFromHost), Serializer<MemoryWriter>{}(roomData), Array{ newPlayer.localID });
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
			reader(roomData);
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


	//P2Body player = world.createCircle(P2Dynamic, Scene::Center(), 15).setFixedRotation(true);

	bool isFirstConnecting = true;


	while (System::Update())
	{
		ClearPrint();

		if(not network.isActive()){
			network.initWhenEnterLobby();
			network.connect(U"player", U"jp");
			network.connectingOrJoining = ConnectingOrJoining::Connecting;
		}
		else {
			network.update();
		}

		if (network.isActive() and not network.isInLobbyOrInRoom() and network.connectingOrJoining == ConnectingOrJoining::Connecting) {
			if (isFirstConnecting) {
				FontAsset(U"message")(U"接続しています...").drawAt(Scene::Center(), Palette::White);
			}
			else {
				FontAsset(U"message")(U"接続が切れました\n再接続しています...").drawAt(Scene::Center(), Palette::White);
			}
		}

		if (network.isActive() and not network.isInLobbyOrInRoom() and network.connectingOrJoining == ConnectingOrJoining::Joining) {
			FontAsset(U"message")(U"入室中...").drawAt(Scene::Center(), Palette::White);
		}

		if (isFirstConnecting and network.isInLobbyOrInRoom()) {
			isFirstConnecting = false;
		}

		if (network.isInLobby()) {
			network.updateLobby();

		}

		if (network.isInRoom()) {
			network.updateRoom();
			network.drawRoom();
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
