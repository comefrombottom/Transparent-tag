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

struct Player {
	Vec2 pos;
	bool isTransparent;
	bool isWatching;
	Color color;
	String name;

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

	void setPlayerPos(LocalPlayerID id, Vec2 pos) {
		m_players[id].pos = pos;
	}

	void addPlayer(LocalPlayerID id, Vec2 pos, Color color, String name) {
		m_players[id] = Player{ pos,false,false,color,name };
	}

	void erasePlayer(LocalPlayerID id) {
		m_players.erase(id);
	}

	template <class Archive>
	void SIV3D_SERIALIZE(Archive& archive)
	{
		archive(m_players,m_traps);
	}
private:
	HashTable<LocalPlayerID,Player> m_players;
	Array<Vec2> m_traps;
};

enum class EventCode {
	roomDataFromHost,
	playerAdd,
	playerErase,
	playerMove,
};

class MyNetwork : public Multiplayer_Photon
{
public:
	using Multiplayer_Photon::Multiplayer_Photon;

	ConnectingOrJoining connectingOrJoining = ConnectingOrJoining::Connecting;

	//Lobby Data
	TextEditState roomName;
	void initWhenEnterLobby() {
		roomName = TextEditState{ U"room" };
	}
	void updateLobby() {

		SimpleGUI::TextBox(roomName, Vec2{ 100,20 }, 200);
		if (SimpleGUI::Button(U"ルームを作成", Vec2{ 100,70 })) {
			initWhenEnterRoom();
			//createRoom(roomName.text + U"#" + ToHex(RandomUint16()), 20);
			createRoom(roomName.text, 20);
			connectingOrJoining = ConnectingOrJoining::Joining;
		}

		for(auto [i,roomName] : Indexed(getRoomNameList())){
			if (SimpleGUI::Button(roomName, Vec2{ 100 + (i % 3) * 200,150 + (i / 3) * 50 })) {
				initWhenEnterRoom();
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
	P2Body player;

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

	void initWhenEnterRoom() {
		hasRoomData = false;
		createWalls();
		player = world.createCircle(P2Dynamic, { 400,300 }, 15).setFixedRotation(true);
	}

	void updateRoom() {
		

		Vec2 inputAxis = Vec2(KeyGroupRight.pressed() - KeyGroupLeft.pressed(), KeyGroupDown.pressed() - KeyGroupUp.pressed());
		Vec2 prePos = player.getPos();
		player.setVelocity(inputAxis * 200);

		for (accumulatedTime += Scene::DeltaTime(); accumulatedTime >= stepTime; accumulatedTime -= stepTime) {
			world.update(stepTime);
		}

		Vec2 pos = player.getPos();
		if (prePos != pos) {
			roomData.setPlayerPos(getLocalPlayerID(), pos);
			sendEvent(FromEnum(EventCode::playerMove), Serializer<MemoryWriter>{}(pos));
		}
	}

	void drawRoom() {
		for (auto& wall : walls) {
			wall.draw();
		}

		for (auto& [id, player] : roomData.players()) {

			if (id == getLocalPlayerID())continue;

			player.pos.asCircle(15).draw(player.color);
			FontAsset(U"message")(player.name).drawAt(player.pos + Vec2{ 0,-20 }, Palette::Gray);
		}

		player.draw(Palette::Beige);


		if (SimpleGUI::Button(U"Exit", Vec2{ 700,20 })) {
			leaveRoom();
		}
	}

	void joinRoomReturn(LocalPlayerID playerID, int32 errorCode, const String& errorString) {
		if (errorCode) {
			if (m_verbose) {
				Console << U"[ルームへの入室でエラーが発生] " << errorString;
			}
			return;
		}

		if (m_verbose) {
			Console << U"[ルーム " << getCurrentRoomName() << U" に入室に成功]";
		}
	}

	void connectReturn([[maybe_unused]] const int32 errorCode, const String& errorString, const String& region, [[maybe_unused]] const String& cluster) override
	{
		initWhenEnterLobby();
	}

	void createRoomReturn([[maybe_unused]] const LocalPlayerID playerID, const int32 errorCode, const String& errorString) override
	{
		if (errorCode)
		{
			if (m_verbose)
			{
				Console << U"[ルームの新規作成でエラーが発生] " << errorString;
			}

			return;
		}

		if (m_verbose)
		{
			Console << U"[ルーム " << getCurrentRoomName() << U" の作成に成功]";
		}
	}

	void joinRoomEventAction(const LocalPlayer& newPlayer, [[maybe_unused]] const Array<LocalPlayerID>& playerIDs, const bool isSelf) override {
		if (isSelf) {
			if (isHost()) {
				roomData.addPlayer(newPlayer.localID, Vec2{ 400,300 }, RandomColor(), newPlayer.userName);
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
			assert(not hasRoomData);
			reader(roomData);
			hasRoomData = true;
			Vec2 pos = Vec2{ 400,300 };
			Color color = RandomColor();
			roomData.addPlayer(getLocalPlayerID(), pos, color, getUserName());
			sendEvent(FromEnum(EventCode::playerAdd), Serializer<MemoryWriter>{}(pos, color, getUserName()));
			break;
		case EventCode::playerAdd:
		{
			if (not hasRoomData) return;
			Vec2 pos;
			Color color;
			String name;
			reader(pos, color, name);
			roomData.addPlayer(playerID, pos, color, name);
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
		default:
			break;
		}

	}
};



void Main()
{
	FontAsset::Register(U"message", 30, Typeface::Bold);


	const std::string secretAppID{ SIV3D_OBFUSCATE(PHOTON_APP_ID) };

	MyNetwork network{ secretAppID, U"1.0", Verbose::Yes };


	//P2Body player = world.createCircle(P2Dynamic, Scene::Center(), 15).setFixedRotation(true);

	bool isFirstConnecting = true;


	while (System::Update())
	{
		ClearPrint();

		if(not network.isActive()){
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

		if(isFirstConnecting and network.isInLobbyOrInRoom()){
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
