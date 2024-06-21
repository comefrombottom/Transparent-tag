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
};

class ShareRoomData {

public:
	ShareRoomData() = default;
private:
	HashTable<LocalPlayerID,Player> players;
	Array<Vec2> traps;

	void setPlayerPos(LocalPlayerID id, Vec2 pos) {
		players[id].pos = pos;
	}
};

class MyNetwork : public Multiplayer_Photon
{
public:
	using Multiplayer_Photon::Multiplayer_Photon;

	ConnectingOrJoining connectingOrJoining = ConnectingOrJoining::Connecting;

	//Lobby Data
	TextEditState roomName;
	void initWhenEnterLobby() {
		roomName = TextEditState{ U"ルーム" };
	}
	void updateLobby() {

		SimpleGUI::TextBox(roomName, Vec2{ 100,50 }, 200);
		if (SimpleGUI::Button(U"ルームを作成", Vec2{ 100,100 })) {
			createRoom(roomName.text + U"#" + ToHex(RandomUint16()), 20);
			connectingOrJoining = ConnectingOrJoining::Joining;
		}

		for(auto [i,roomName] : Indexed(getRoomNameList())){
			if (SimpleGUI::Button(roomName, Vec2{ 100 + (i%3)*200,150 + (i/3) * 50 })) {
				joinRoom(roomName);
				connectingOrJoining = ConnectingOrJoining::Joining;
			}
		}
	}
	

	//Room Data
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
		createWalls();
		player = world.createCircle(P2Dynamic, Scene::Center(), 15).setFixedRotation(true);
	}

	void updateRoom() {
		if(SimpleGUI::Button(U"Exit", Vec2{ 700,20 })){
			leaveRoom();
		}

		Vec2 inputAxis = Vec2(KeyGroupRight.pressed() - KeyGroupLeft.pressed(), KeyGroupDown.pressed() - KeyGroupUp.pressed());

	}

	void drawRoom() {
		for (auto& wall : walls) {
			wall.draw();
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
			initWhenEnterRoom();
		}
		else {
		}
	}

	void leaveRoomEventAction(const LocalPlayerID playerID, [[maybe_unused]] const bool isInactive) override {

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
			Print << U"ロビーにいます";
			network.updateLobby();

		}

		if (network.isInRoom()) {
			Print << U"ルームにいます";
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
