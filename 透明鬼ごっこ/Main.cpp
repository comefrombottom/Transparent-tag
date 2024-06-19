# include <Siv3D.hpp> // Siv3D v0.6.14

void Main()
{


	P2World world(0);

	constexpr double stepTime = 1.0 / 200.0;

	double accumulatedTime = 0.0;

	Array<P2Body> walls;

	walls << world.createRect(P2Static, Scene::Rect().topCenter(), Size{ Scene::Width(),100 });
	walls << world.createRect(P2Static, Scene::Rect().bottomCenter(), Size{ Scene::Width(),100 });
	walls << world.createRect(P2Static, Scene::Rect().leftCenter(), Size{ 100,Scene::Height() });
	walls << world.createRect(P2Static, Scene::Rect().rightCenter(), Size{ 100,Scene::Height() });

	walls << world.createRect(P2Static, Vec2{ 300,300 }, Size{ 100,100 });
	walls << world.createRect(P2Static, Vec2{ 500,300 }, Size{ 100,100 });
	walls << world.createRect(P2Static, Vec2{ 400,400 }, Size{ 100,100 });


	P2Body player = world.createCircle(P2Dynamic, Scene::Center(), 15).setFixedRotation(true);

	InputGroup KeyGroupLeft{ KeyLeft, KeyA };
	InputGroup KeyGroupRight{ KeyRight, KeyD };
	InputGroup KeyGroupUp{ KeyUp, KeyW };
	InputGroup KeyGroupDown{ KeyDown, KeyS };

	bool isTransparent = false;

	while (System::Update())
	{
		for(accumulatedTime += Scene::DeltaTime(); accumulatedTime >= stepTime; accumulatedTime -= stepTime)
		{
			world.update(stepTime);
		}

		Vec2 inputAxis = Vec2(KeyGroupRight.pressed() - KeyGroupLeft.pressed(), KeyGroupDown.pressed() - KeyGroupUp.pressed());
		isTransparent = KeySpace.pressed();


		double speed = 200;
		if(isTransparent)speed = 120;

		if(not inputAxis.isZero())
		{
			player.setVelocity(inputAxis.normalized() * speed);
		}
		else {
			player.setVelocity({0,0});
		}



		if(isTransparent)
		{
			player.draw(ColorF(1, 0.5));
		}
		else
		{
			player.draw(ColorF(1, 1));
		}

		for (auto& wall : walls)
		{
			wall.draw();
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
