#include "mainwindow.h"

#include <QColor>
#include <QFont>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <Qt>

#include <algorithm>
#include <cmath>

namespace {
// 作業指定畫面大小固定為 1620 x 1080。
const int kScreenWidth = 1620;
const int kScreenHeight = 1080;

// Stage 1 有 3 個 frames；Stage 2 有 5 個 frames。
const int kStage1Width = kScreenWidth * 3;
const int kStage2Width = kScreenWidth * 5;

// 地板頂端的位置。Kirby 的腳碰到這條線，就代表站在地上。
const int kGroundTop = 840;

// 先用固定的 Kirby 碰撞大小，方便 Demo 與碰撞判斷。
const int kKirbyWidth = 135;
const int kKirbyHeight = 122;

// Kirby 的基本移動數值。
const float kMoveSpeed = 8.0f;
const float kJumpSpeed = -28.0f;
const float kGravity = 1.35f;
const float kMaxFallSpeed = 28.0f;

// 飛行狀態的數值：按 Up 會上升，不按 Up 會慢慢下落。
const float kHoverLift = 1.5f;
const float kHoverRiseSpeed = -8.0f;
const float kHoverGravity = 0.45f;
const float kHoverMaxFallSpeed = 5.5f;

// 不讓 Kirby 飛到畫面最上方之外。
const float kTopLimit = 40.0f;

// Step 4 生命系統：初始 3 lives，每條 life 有 3 HP。
const int kInitialLives = 3;
// 1UP 最多只能把 lives 補到 3，不能無限增加。
const int kMaxLives = 3;
const int kMaxHp = 3;

// QTimer 大約 60 FPS，所以 120 幀約等於 2 秒無敵時間。
const int kInvincibleFrames = 120;
// 道具在畫面上的繪製大小，也用同一個矩形做碰撞判斷。
const int kItemSize = 76;
// Stage 1 敵人的碰撞與繪製大小，先固定成容易測試的矩形。
const int kWaddleDeeWidth = 94;
const int kWaddleDeeHeight = 86;
const int kGordoSize = 82;
const float kWaddleDeeSpeed = 2.0f;
const float kGordoSpeed = 2.4f;
// 吸入範圍與星星彈大小先用清楚的矩形，方便測試碰撞。
const int kInhaleWidth = 280;
const int kInhaleHeight = 150;
const int kSpitStarSize = 52;
const float kSpitStarSpeed = 16.0f;
// Hover 中按 X 吐氣時，讓 exhale 圖片短暫停留幾幀，方便玩家看出動作。
const int kExhaleEffectFrames = 14;
// Mouthful 按 X 吐出星星彈時，讓 Kirby 的 Spit 圖停留約 0.3 秒。
const int kSpitAnimationFrames = 18;
// 跑步約每 8 個 game tick 換一張，避免動畫閃太快。
const int kRunAnimationTicks = 8;
// Hover 兩張圖慢慢循環，讓飛行有鼓起/收縮的感覺。
const int kHoverAnimationTicks = 10;

float clampFloat(float value, float minValue, float maxValue)
{
    return std::max(minValue, std::min(value, maxValue));
}

QPolygonF makeStarPolygon(const QRectF &target)
{
    // Dataset 若沒有星星彈圖片，就用這個五角星當 placeholder，不再畫成圓球。
    QPolygonF star;
    const QPointF center = target.center();
    const double outerRadius = std::min(target.width(), target.height()) / 2.0;
    const double innerRadius = outerRadius * 0.45;
    const double pi = 3.14159265358979323846;

    for (int i = 0; i < 10; ++i) {
        const double radius = (i % 2 == 0) ? outerRadius : innerRadius;
        const double angle = -pi / 2.0 + i * pi / 5.0;
        star << QPointF(center.x() + std::cos(angle) * radius,
                        center.y() + std::sin(angle) * radius);
    }

    return star;
}

const QPixmap &animationFrame(const QVector<QPixmap> &frames, int frameIndex, const QPixmap &fallback)
{
    if (frames.isEmpty()) {
        return fallback;
    }

    const QPixmap &pixmap = frames.at(frameIndex % frames.size());
    return pixmap.isNull() ? fallback : pixmap;
}
}

// 建構子：建立遊戲視窗時第一個執行。
// 這裡會設定視窗大小、載入素材、建立關卡資料，並啟動 QTimer 遊戲迴圈。
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_scene(Scene::StartMenu),
      m_leftPressed(false),
      m_rightPressed(false),
      m_upPressed(false),
      m_downPressed(false),
      m_faceRight(true),
      m_onGround(true),
      m_isHovering(false),
      m_canStartHover(true),
      m_kirbyState(KirbyState::Normal),
      m_hasMouthfulEnemy(false),
      m_mouthfulEnemyType(EnemyType::WaddleDee),
      m_lives(kInitialLives),
      m_hp(kMaxHp),
      m_invincibleFrames(0),
      m_gameOverSelectQuit(false),
      m_exhaleEffectFrames(0),
      m_spitAnimationFrames(0),
      m_animationTimer(0),
      m_kirbyX(120.0f),
      m_kirbyY(static_cast<float>(kGroundTop - kKirbyHeight)),
      m_kirbyVelocityY(0.0f),
      m_cameraX(0.0f)
{
    setWindowTitle(QStringLiteral("KirbyDemo"));
    setFixedSize(kScreenWidth, kScreenHeight);

    // 讓 MainWindow 可以直接接收鍵盤事件。
    setFocusPolicy(Qt::StrongFocus);
    // Game Over 選單需要偵測滑鼠滑過 Continue / Quit，不按滑鼠也能更新圖片。
    setMouseTracking(true);

    loadAssets();
    setupStages();
    setupItems();
    setupEnemies();
    resetStageStart();

    // 每 16 毫秒更新一次，大約等於 60 FPS。
    connect(&m_timer, &QTimer::timeout, this, [this]() {
        updateGame();
    });
    m_timer.start(16);
}

// 載入所有會用到的圖片。
// 遊戲啟動時呼叫一次，之後繪圖時就直接使用已載入的 QPixmap。
void MainWindow::loadAssets()
{
    // 從 assets.qrc 載入圖片，路徑前面的 :/ 代表 Qt 內建資源。
    m_startBackground.load(QStringLiteral(":/Project2_Dataset/Image/background/start.png"));
    m_stageBackdrop.load(QStringLiteral(":/Project2_Dataset/Image/background/supplement(1).jpg"));
    m_stage1Frame1.load(QStringLiteral(":/Project2_Dataset/Image/background/Stage1(1).png"));
    m_stage1Frame2.load(QStringLiteral(":/Project2_Dataset/Image/background/Stage1(2).png"));
    m_stage1Frame3.load(QStringLiteral(":/Project2_Dataset/Image/background/Stage1(3).png"));
    m_stage2Frame1.load(QStringLiteral(":/Project2_Dataset/Image/background/Stage2(1).png"));
    m_stage2Frame2.load(QStringLiteral(":/Project2_Dataset/Image/background/Stage2(2).png"));
    m_gameOverContinueImage.load(QStringLiteral(":/Project2_Dataset/Image/background/game_over_continue.png"));
    m_gameOverQuitImage.load(QStringLiteral(":/Project2_Dataset/Image/background/game_over_quit.png"));
    m_floorTile.load(QStringLiteral(":/Project2_Dataset/Image/item/floor.png"));
    m_blockTile.load(QStringLiteral(":/Project2_Dataset/Image/item/brick.png"));
    m_portalImage.load(QStringLiteral(":/Project2_Dataset/Image/item/door.png"));
    m_goalImage.load(QStringLiteral(":/Project2_Dataset/Image/item/goal_door.png"));
    m_lifeIcon.load(QStringLiteral(":/Project2_Dataset/Image/item/life.png"));
    m_hpFullIcon.load(QStringLiteral(":/Project2_Dataset/Image/item/HP_1.png"));
    m_hpEmptyIcon.load(QStringLiteral(":/Project2_Dataset/Image/item/HP_0.png"));
    m_maximTomatoImage.load(QStringLiteral(":/Project2_Dataset/Image/item/Maxim Tomato.png"));
    m_oneUpImage.load(QStringLiteral(":/Project2_Dataset/Image/item/1UP.png"));
    m_waddleDeeImage.load(QStringLiteral(":/Project2_Dataset/Image/Waddle Dee/Waddle_Dee_0.png"));
    m_gordoImage.load(QStringLiteral(":/Project2_Dataset/Image/Gordo/Gordo(0).png"));
    m_kirbyStopLeft.load(QStringLiteral(":/Project2_Dataset/Image/Kirby_normal/kirby_stop_L.png"));
    m_kirbyStopRight.load(QStringLiteral(":/Project2_Dataset/Image/Kirby_normal/kirby_stop_R.png"));
    m_kirbyRunLeft.load(QStringLiteral(":/Project2_Dataset/Image/Kirby_normal/kirby_run_1_L.png"));
    m_kirbyRunRight.load(QStringLiteral(":/Project2_Dataset/Image/Kirby_normal/kirby_run_1_R.png"));
    m_kirbyJump.load(QStringLiteral(":/Project2_Dataset/Image/Kirby_normal/kirby_jump(1).png"));
    // 跑步、跳躍、Hover 都整理成 frame 陣列，currentKirbyPixmap() 會依狀態挑選要顯示哪一張。
    m_kirbyRunLeftFrames.clear();
    m_kirbyRunLeftFrames
        << QPixmap(QStringLiteral(":/Project2_Dataset/Image/Kirby_normal/kirby_run_1_L.png"))
        << QPixmap(QStringLiteral(":/Project2_Dataset/Image/Kirby_normal/kirby_run_2_L.png"))
        << QPixmap(QStringLiteral(":/Project2_Dataset/Image/Kirby_normal/kirby_run_3_L.png"))
        << QPixmap(QStringLiteral(":/Project2_Dataset/Image/Kirby_normal/kirby_run_4_L.png"));
    m_kirbyRunRightFrames.clear();
    m_kirbyRunRightFrames
        << QPixmap(QStringLiteral(":/Project2_Dataset/Image/Kirby_normal/kirby_run_1_R.png"))
        << QPixmap(QStringLiteral(":/Project2_Dataset/Image/Kirby_normal/kirby_run_2_R.png"))
        << QPixmap(QStringLiteral(":/Project2_Dataset/Image/Kirby_normal/kirby_run_3_R.png"))
        << QPixmap(QStringLiteral(":/Project2_Dataset/Image/Kirby_normal/kirby_run_4_R.png"));
    m_kirbyJumpFrames.clear();
    m_kirbyJumpFrames
        << QPixmap(QStringLiteral(":/Project2_Dataset/Image/Kirby_normal/kirby_jump(1).png"))
        << QPixmap(QStringLiteral(":/Project2_Dataset/Image/Kirby_normal/kirby_jump(2).png"))
        << QPixmap(QStringLiteral(":/Project2_Dataset/Image/Kirby_normal/kirby_jump(3).png"));
    m_kirbyHoverLeftFrames.clear();
    m_kirbyHoverLeftFrames
        << QPixmap(QStringLiteral(":/Project2_Dataset/Image/Kirby_normal/kirby_fly_1_L.png"))
        << QPixmap(QStringLiteral(":/Project2_Dataset/Image/Kirby_normal/kirby_fly_2_L.png"));
    m_kirbyHoverRightFrames.clear();
    m_kirbyHoverRightFrames
        << QPixmap(QStringLiteral(":/Project2_Dataset/Image/Kirby_normal/kirby_fly_1_R.png"))
        << QPixmap(QStringLiteral(":/Project2_Dataset/Image/Kirby_normal/kirby_fly_2_R.png"));
    m_kirbySquatLeft.load(QStringLiteral(":/Project2_Dataset/Image/Kirby_normal/kirby_down_L.png"));
    m_kirbySquatRight.load(QStringLiteral(":/Project2_Dataset/Image/Kirby_normal/kirby_down_R.png"));
    m_kirbyHoverLeft.load(QStringLiteral(":/Project2_Dataset/Image/Kirby_normal/kirby_fly_1_L.png"));
    m_kirbyHoverRight.load(QStringLiteral(":/Project2_Dataset/Image/Kirby_normal/kirby_fly_1_R.png"));
    m_kirbyInhaleLeft.load(QStringLiteral(":/Project2_Dataset/Image/Kirby_normal/kirby_attack_L.png"));
    m_kirbyInhaleRight.load(QStringLiteral(":/Project2_Dataset/Image/Kirby_normal/kirby_attack_R.png"));
    m_kirbyExhaleLeft.load(QStringLiteral(":/Project2_Dataset/Image/Kirby_normal/kirby_attack_star_L(1).png"));
    m_kirbyExhaleRight.load(QStringLiteral(":/Project2_Dataset/Image/Kirby_normal/kirby_attack_star_R(1).png"));
    // Kirby Spit 動作使用老師指定的吐星星圖片，和 projectile 的星星本體分開處理。
    m_kirbySpitLeft.load(QStringLiteral(":/Project2_Dataset/Image/Kirby_normal/kirby_attack_star_L(1).png"));
    m_kirbySpitRight.load(QStringLiteral(":/Project2_Dataset/Image/Kirby_normal/kirby_attack_star_R(1).png"));
    // Dataset 沒有明確命名 mouthful 的圖，先使用鼓起飛行圖當嘴裡含敵人的視覺 placeholder。
    m_kirbyMouthfulLeft.load(QStringLiteral(":/Project2_Dataset/Image/Kirby_normal/kirby_fly_2_L.png"));
    m_kirbyMouthfulRight.load(QStringLiteral(":/Project2_Dataset/Image/Kirby_normal/kirby_fly_2_R.png"));
    m_spitStarImage.load(QStringLiteral(":/Project2_Dataset/Image/Kirby_normal/kirby_attack_star(2).png"));
}

// 建立兩個關卡的碰撞資料。
// platforms、blocks、holes、portal、goal 都是 QRectF，碰撞只看這些矩形，不看背景圖片。
void MainWindow::setupStages()
{
    // Stage 1：3 個 frames，每個 frame 至少 2 個平台、1 個 block。
    m_stage1.width = kStage1Width;
    m_stage1.platforms.clear();
    m_stage1.platforms
        << QRectF(320, 650, 320, 42) << QRectF(900, 535, 330, 42)
        << QRectF(1850, 620, 320, 42) << QRectF(2500, 500, 330, 42)
        << QRectF(3420, 610, 320, 42) << QRectF(4050, 500, 330, 42);

    m_stage1.blocks.clear();
    m_stage1.blocks
        << QRectF(1280, kGroundTop - 90, 90, 90)
        << QRectF(760, 500, 90, 90)
        << QRectF(2920, kGroundTop - 90, 90, 90)
        << QRectF(2240, 430, 90, 90)
        << QRectF(3740, kGroundTop - 90, 90, 90)
        << QRectF(4440, 560, 90, 90);

    // 整個遊戲至少要有 1 個 hole，這裡放在 Stage 1 第二個 frame。
    m_stage1.holes.clear();
    m_stage1.holes << QRectF(2220, kGroundTop, 230, kScreenHeight - kGroundTop);

    m_stage1.portal = QRectF(kStage1Width - 230, kGroundTop - 180, 130, 180);
    m_stage1.goal = QRectF();

    // Stage 2：5 個 frames，每個 frame 至少 2 個平台、1 個 block。
    m_stage2.width = kStage2Width;
    m_stage2.platforms.clear();
    m_stage2.platforms
        << QRectF(300, 640, 320, 42) << QRectF(950, 520, 320, 42)
        << QRectF(1920, 650, 320, 42) << QRectF(2620, 520, 320, 42)
        << QRectF(3520, 610, 320, 42) << QRectF(4200, 490, 320, 42)
        << QRectF(5150, 640, 320, 42) << QRectF(5850, 520, 320, 42)
        << QRectF(6740, 610, 320, 42) << QRectF(7380, 500, 320, 42);

    m_stage2.blocks.clear();
    m_stage2.blocks
        << QRectF(1220, kGroundTop - 90, 90, 90)
        << QRectF(760, 500, 90, 90)
        << QRectF(2360, kGroundTop - 90, 90, 90)
        << QRectF(2980, 470, 90, 90)
        << QRectF(3920, kGroundTop - 90, 90, 90)
        << QRectF(4620, 540, 90, 90)
        << QRectF(5480, kGroundTop - 90, 90, 90)
        << QRectF(6240, 500, 90, 90)
        << QRectF(7040, kGroundTop - 90, 90, 90)
        << QRectF(7640, 430, 90, 90);

    m_stage2.holes.clear();
    m_stage2.portal = QRectF();
    m_stage2.goal = QRectF(kStage2Width - 240, kGroundTop - 190, 140, 190);
}

// 建立 Stage 1 的兩個道具。
// 遊戲一開始或 Continue 重新開始時呼叫，會把 collected 重設為 false。
void MainWindow::setupItems()
{
    m_items.clear();

    // Maxim Tomato 放在第一個平台上，玩家跳上平台即可測試補滿 HP。
    m_items << Item{QRectF(470, 650 - kItemSize, kItemSize, kItemSize),
                    ItemType::MaximTomato,
                    false};

    // 1UP 放在 hole 後方的地面，玩家可先掉洞扣 life，再回來吃它測試 lives 回到上限。
    m_items << Item{QRectF(2700, kGroundTop - kItemSize, kItemSize, kItemSize),
                    ItemType::OneUp,
                    false};
}

// 建立 Stage 1 的敵人配置。
// 這是教學關卡用配置：多放 Waddle Dee 讓玩家練習移動、受傷、吸入與吐星星彈。
// Gordo 數量較少，主要提醒玩家它是不可吸入、不可消滅的危險障礙。
void MainWindow::setupEnemies()
{
    m_enemies.clear();

    // Frame 1：2 隻 Waddle Dee，位置偏簡單，讓玩家先練習吸入與受傷判定。
    m_enemies << Enemy{QRectF(520, kGroundTop - kWaddleDeeHeight, kWaddleDeeWidth, kWaddleDeeHeight),
                       EnemyType::WaddleDee,
                       430.0f,
                       760.0f,
                       0.0f,
                       0.0f,
                       kWaddleDeeSpeed,
                       1,
                       true,
                       true};
    m_enemies << Enemy{QRectF(1040, kGroundTop - kWaddleDeeHeight, kWaddleDeeWidth, kWaddleDeeHeight),
                       EnemyType::WaddleDee,
                       930.0f,
                       1400.0f,
                       0.0f,
                       0.0f,
                       kWaddleDeeSpeed,
                       1,
                       true,
                       true};

    // Frame 2：2 隻 Waddle Dee + 1 個 Gordo，避開 hole，讓玩家練習跳過洞後繼續戰鬥。
    m_enemies << Enemy{QRectF(1760, kGroundTop - kWaddleDeeHeight, kWaddleDeeWidth, kWaddleDeeHeight),
                       EnemyType::WaddleDee,
                       1680.0f,
                       2050.0f,
                       0.0f,
                       0.0f,
                       kWaddleDeeSpeed,
                       1,
                       true,
                       true};
    m_enemies << Enemy{QRectF(3070, kGroundTop - kWaddleDeeHeight, kWaddleDeeWidth, kWaddleDeeHeight),
                       EnemyType::WaddleDee,
                       3020.0f,
                       3220.0f,
                       0.0f,
                       0.0f,
                       kWaddleDeeSpeed,
                       -1,
                       true,
                       true};
    m_enemies << Enemy{QRectF(2050, 430, kGordoSize, kGordoSize),
                       EnemyType::Gordo,
                       0.0f,
                       0.0f,
                       380.0f,
                       570.0f,
                       kGordoSpeed,
                       1,
                       false,
                       true};

    // Frame 3：保留第 1 與第 3 隻 Waddle Dee，中間留空間讓玩家練習跳躍與吐星星彈。
    m_enemies << Enemy{QRectF(3350, kGroundTop - kWaddleDeeHeight, kWaddleDeeWidth, kWaddleDeeHeight),
                       EnemyType::WaddleDee,
                       3280.0f,
                       3600.0f,
                       0.0f,
                       0.0f,
                       kWaddleDeeSpeed,
                       1,
                       true,
                       true};
    // 右側 Waddle Dee 移到原本 Gordo 附近，讓中間區域可以練習吸入與星星彈。
    m_enemies << Enemy{QRectF(4050, kGroundTop - kWaddleDeeHeight, kWaddleDeeWidth, kWaddleDeeHeight),
                       EnemyType::WaddleDee,
                       3980.0f,
                       4230.0f,
                       0.0f,
                       0.0f,
                       kWaddleDeeSpeed,
                       -1,
                       true,
                       true};
    // Gordo 移到原本右側 Waddle Dee 附近，沿平台底部與地板之間固定上下移動。
    // 最低點用地板高度 kGroundTop 設定，讓 Gordo bottom 貼近地板但不穿進地板。
    m_enemies << Enemy{QRectF(4300, 545, kGordoSize, kGordoSize),
                       EnemyType::Gordo,
                       0.0f,
                       0.0f,
                       545.0f,
                       static_cast<float>(kGroundTop - 2),
                       kGordoSpeed,
                       1,
                       false,
                       true};
}

// 把 Kirby 放回目前關卡起點。
// 進入新關卡、掉進 hole 或掉出畫面下方時會呼叫。
void MainWindow::resetStageStart()
{
    // 掉入 hole、切換關卡或 Continue 時，把 Kirby 放回目前 stage 起點。
    m_kirbyX = 120.0f;
    m_kirbyY = static_cast<float>(kGroundTop - kKirbyHeight);
    m_kirbyVelocityY = 0.0f;
    m_cameraX = 0.0f;

    m_leftPressed = false;
    m_rightPressed = false;
    m_upPressed = false;
    m_downPressed = false;
    m_faceRight = true;
    m_onGround = true;
    m_isHovering = false;
    m_canStartHover = true;
    m_kirbyState = KirbyState::Normal;
    m_hasMouthfulEnemy = false;
    m_exhaleEffectFrames = 0;
    m_spitAnimationFrames = 0;
    m_animationTimer = 0;
    m_projectiles.clear();
}

// 每一幀更新遊戲狀態。
// QTimer 約每 16ms 呼叫一次；更新完 Kirby 與 camera 後，呼叫 update() 要求重畫畫面。
void MainWindow::updateGame()
{
    // 只有 Stage 1 / Stage 2 需要更新 Kirby 和鏡頭。
    if (m_scene == Scene::Stage1 || m_scene == Scene::Stage2) {
        // 無敵時間用幀數倒數；倒數期間 damageKirby() 不會扣 HP。
        if (m_invincibleFrames > 0) {
            --m_invincibleFrames;
        }
        if (m_exhaleEffectFrames > 0) {
            --m_exhaleEffectFrames;
        }
        if (m_spitAnimationFrames > 0) {
            --m_spitAnimationFrames;
        }
        ++m_animationTimer;

        updateKirby();

        // 掉洞可能會切到 Game Over；確認仍在關卡中才檢查道具與 camera。
        if (m_scene == Scene::Stage1 || m_scene == Scene::Stage2) {
            updateItems();
            updateEnemies();
            updateProjectiles();

            if (m_scene == Scene::Stage1 || m_scene == Scene::Stage2) {
                updateCamera();
            }
        }
    }

    update();
}

// 更新 Kirby 的物理與碰撞。
// 這裡處理左右移動、跳躍、Hover、重力、平台落地、Block 阻擋、hole 重生。
void MainWindow::updateKirby()
{
    const bool isSquatting = m_downPressed && m_onGround && !m_isHovering;
    const float previousX = m_kirbyX;
    const float previousY = m_kirbyY;
    const float previousTop = previousY;
    const float previousBottom = previousY + kKirbyHeight;

    // Down 只代表蹲下姿勢，不會改變 y，也不會觸發穿越平台。
    // 蹲下時不左右移動；站立、跳躍、飛行時才處理水平移動。
    if (!isSquatting) {
        if (m_leftPressed && !m_rightPressed) {
            m_kirbyX -= kMoveSpeed;
            m_faceRight = false;
        } else if (m_rightPressed && !m_leftPressed) {
            m_kirbyX += kMoveSpeed;
            m_faceRight = true;
        }
    }

    // Kirby 不能走出目前 stage 的左右邊界。
    m_kirbyX = clampFloat(m_kirbyX, 0.0f, static_cast<float>(currentStage().width - kKirbyWidth));

    // Block 是實心障礙物，左右移動撞到就退回原本的 x。
    // Demo 時可以說：這是側面碰撞，碰到牆就停住。
    if (collidesWithAnyBlock(kirbyRect())) {
        m_kirbyX = previousX;
    }

    if (m_isHovering) {
        // Hover 中按住 Up 往上推，不按 Up 就慢慢下落。
        if (m_upPressed) {
            m_kirbyVelocityY = std::max(m_kirbyVelocityY - kHoverLift, kHoverRiseSpeed);
        } else {
            m_kirbyVelocityY = std::min(m_kirbyVelocityY + kHoverGravity, kHoverMaxFallSpeed);
        }
        m_kirbyY += m_kirbyVelocityY;
    } else if (!m_onGround) {
        // 一般跳躍或下落狀態會受到正常重力影響。
        m_kirbyVelocityY = std::min(m_kirbyVelocityY + kGravity, kMaxFallSpeed);
        m_kirbyY += m_kirbyVelocityY;
    }

    if (m_kirbyY < kTopLimit) {
        m_kirbyY = kTopLimit;
        if (m_kirbyVelocityY < 0.0f) {
            m_kirbyVelocityY = 0.0f;
        }
    }

    float landingTop = 100000.0f;
    bool landed = false;

    // Platform 是 one-way：只有「上一幀 bottom 在平台 top 上方」
    // 且「這一幀 bottom 掉過平台 top」時才會站上去。
    // 因此 Kirby 從下方往上跳時可以穿過平台。
    if (m_kirbyVelocityY >= 0.0f) {
        const QRectF movedRect = kirbyRect();

        for (const QRectF &platform : currentStage().platforms) {
            if (previousBottom <= platform.top()
                    && movedRect.bottom() >= platform.top()
                    && horizontalOverlap(movedRect, platform)) {
                landingTop = std::min(landingTop, static_cast<float>(platform.top()));
                landed = true;
            }
        }

        // Block 的上方也可以站立，但 Block 本身會阻擋左右與上下穿越。
        for (const QRectF &block : currentStage().blocks) {
            if (previousBottom <= block.top()
                    && movedRect.bottom() >= block.top()
                    && horizontalOverlap(movedRect, block)) {
                landingTop = std::min(landingTop, static_cast<float>(block.top()));
                landed = true;
            }
        }
    }

    if (landed) {
        m_kirbyY = landingTop - kKirbyHeight;
        m_kirbyVelocityY = 0.0f;
        m_onGround = true;
        // Hover 像原版 Kirby 一樣：落到平台或 Block 上仍維持鼓起狀態。
        // 因此這裡不能把 m_isHovering 設成 false，只有按 X Exhale 才會解除。
        m_canStartHover = !m_isHovering;
    } else if (m_kirbyVelocityY < 0.0f) {
        // 從下方撞到 Block 時，不能穿過去；Platform 不會擋住從下往上穿越。
        for (const QRectF &block : currentStage().blocks) {
            const QRectF rect = kirbyRect();
            if (previousTop >= block.bottom()
                    && rect.top() <= block.bottom()
                    && horizontalOverlap(rect, block)) {
                m_kirbyY = block.bottom();
                m_kirbyVelocityY = 0.0f;
                break;
            }
        }
    }

    // Ground 是實心地板：只要不是 hole 區域，就永遠不能穿過。
    // Hole 的效果只靠 isOverHole()，不是靠 Down 鍵。
    QRectF rect = kirbyRect();
    if (rect.bottom() >= kGroundTop && !isOverHole(rect)) {
        m_kirbyY = static_cast<float>(kGroundTop - kKirbyHeight);
        m_kirbyVelocityY = 0.0f;
        m_onGround = true;
        // Hover 落到實心地板時也不自動解除；外觀仍顯示 fly / hover 圖片。
        m_canStartHover = !m_isHovering;
    }

    // 如果 Kirby 站著但腳下沒有平台、Block 或地板，就開始下落。
    rect = kirbyRect();
    if (m_onGround && !hasGroundSupport(rect)) {
        m_onGround = false;
    }

    // 掉入 hole 或掉出畫面下方時，扣 1 life 再回到目前 stage 起點。
    rect = kirbyRect();
    if ((isOverHole(rect) && rect.bottom() > kGroundTop + 90) || rect.top() > kScreenHeight) {
        loseLife();
    }
}

// 檢查 Kirby 是否碰到 Stage 1 的道具。
// 每一幀更新 Kirby 後呼叫；如果碰到道具，就立刻套用效果並標記為已收集。
void MainWindow::updateItems()
{
    if (m_scene != Scene::Stage1) {
        return;
    }

    const QRectF kirbyCollisionRect = kirbyRect();
    for (Item &item : m_items) {
        if (item.collected) {
            continue;
        }

        // 道具碰撞只看 Kirby 的 collision rect 和 item rect，不使用背景圖片判斷。
        if (kirbyCollisionRect.intersects(item.rect)) {
            applyItemEffect(item.type);
            item.collected = true;
        }
    }
}

// 套用道具效果。
// Maxim Tomato 會把 HP 補滿；1UP 會增加 lives，但不超過 3。
void MainWindow::applyItemEffect(ItemType itemType)
{
    if (itemType == ItemType::MaximTomato) {
        m_hp = kMaxHp;
    } else if (itemType == ItemType::OneUp) {
        m_lives = std::min(kMaxLives, m_lives + 1);
    }
}

// 更新 Stage 1 敵人。
// Waddle Dee 會左右巡邏並撞牆轉向；Gordo 是不可消滅的障礙，只沿固定軌跡移動。
void MainWindow::updateEnemies()
{
    if (m_scene != Scene::Stage1) {
        return;
    }

    for (Enemy &enemy : m_enemies) {
        if (!enemy.active) {
            continue;
        }

        if (enemy.type == EnemyType::WaddleDee) {
            const float previousX = static_cast<float>(enemy.rect.left());
            const int previousDirection = enemy.direction;
            bool hitBlock = false;

            // Waddle Dee 是一般敵人：只在自己的 patrol 範圍內左右走。
            enemy.rect.translate(enemy.speed * enemy.direction, 0.0);

            // 如果 Waddle Dee 撞到 Block，就退回上一個位置並轉向，避免卡進牆裡。
            for (const QRectF &block : m_stage1.blocks) {
                if (enemy.rect.intersects(block)) {
                    hitBlock = true;
                    break;
                }
            }

            if (hitBlock) {
                enemy.rect.moveLeft(previousX);
                enemy.direction = -previousDirection;
            } else if (enemy.rect.left() < enemy.minX) {
                enemy.rect.moveLeft(enemy.minX);
                enemy.direction = 1;
            } else if (enemy.rect.right() > enemy.maxX) {
                enemy.rect.moveRight(enemy.maxX);
                enemy.direction = -1;
            } else if (enemy.rect.left() < 0.0f) {
                enemy.rect.moveLeft(0.0f);
                enemy.direction = 1;
            } else if (enemy.rect.right() > m_stage1.width) {
                enemy.rect.moveRight(m_stage1.width);
                enemy.direction = -1;
            }
        } else if (enemy.type == EnemyType::Gordo) {
            // Gordo 是刺球型障礙物：不能吸入、不能消滅，只能避開。
            if (enemy.horizontalMove) {
                enemy.rect.translate(enemy.speed * enemy.direction, 0.0);

                if (enemy.rect.left() < enemy.minX) {
                    enemy.rect.moveLeft(enemy.minX);
                    enemy.direction = 1;
                } else if (enemy.rect.right() > enemy.maxX) {
                    enemy.rect.moveRight(enemy.maxX);
                    enemy.direction = -1;
                }
            } else {
                enemy.rect.translate(0.0, enemy.speed * enemy.direction);

                if (enemy.rect.top() < enemy.minY) {
                    enemy.rect.moveTop(enemy.minY);
                    enemy.direction = 1;
                } else if (enemy.rect.bottom() > enemy.maxY) {
                    enemy.rect.moveBottom(enemy.maxY);
                    enemy.direction = -1;
                }
            }
        }
    }

    // 敵人移動後再檢查吸入，這樣 Waddle Dee 走進吸力範圍時可以被吸進嘴裡。
    updateInhale();

    const QRectF kirbyCollisionRect = kirbyRect();
    for (const Enemy &enemy : m_enemies) {
        if (!enemy.active) {
            continue;
        }

        // 敵人不會推動 Kirby，也不會讓 Kirby 卡住；碰到只呼叫傷害函式。
        if (kirbyCollisionRect.intersects(enemy.rect)) {
            damageKirby();
        }
    }
}

// 更新吸入判定。
// Waddle Dee 是一般敵人，所以可以被吸入；Gordo 是特殊危險障礙物，不能被吸入或消滅。
void MainWindow::updateInhale()
{
    if (m_scene != Scene::Stage1 || m_kirbyState != KirbyState::Inhaling) {
        return;
    }

    const QRectF suctionRect = inhaleRect();
    for (Enemy &enemy : m_enemies) {
        if (!enemy.active || !suctionRect.intersects(enemy.rect)) {
            continue;
        }

        if (enemy.type == EnemyType::WaddleDee) {
            // Waddle Dee 沒有複製能力；吸入後 Kirby 只會進入 Mouthful，吞下也不會得到能力。
            enemy.active = false;
            m_kirbyState = KirbyState::Mouthful;
            m_hasMouthfulEnemy = true;
            m_mouthfulEnemyType = EnemyType::WaddleDee;
            m_isHovering = false;
            m_canStartHover = false;
            return;
        }

        // Gordo 是刺球型障礙，不屬於可吸入敵人；進入吸力範圍也不會消失。
    }
}

// 更新星星彈。
// 星星彈會水平移動，碰到 Waddle Dee 會一起消失；碰到 Gordo 只會自己消失。
void MainWindow::updateProjectiles()
{
    if (m_scene != Scene::Stage1) {
        return;
    }

    for (Projectile &projectile : m_projectiles) {
        if (!projectile.active) {
            continue;
        }

        projectile.rect.translate(projectile.velocityX, 0.0);

        const bool outsideStage = projectile.rect.right() < 0.0
                || projectile.rect.left() > m_stage1.width;
        const bool farFromCamera = projectile.rect.right() < m_cameraX - kScreenWidth
                || projectile.rect.left() > m_cameraX + kScreenWidth * 2;
        if (outsideStage || farFromCamera) {
            projectile.active = false;
            continue;
        }

        // 星星彈不能穿過 solid Block，碰到 Block 會立刻消失。
        for (const QRectF &block : m_stage1.blocks) {
            if (projectile.rect.intersects(block)) {
                projectile.active = false;
                break;
            }
        }

        if (!projectile.active) {
            continue;
        }

        for (Enemy &enemy : m_enemies) {
            if (!enemy.active || !projectile.rect.intersects(enemy.rect)) {
                continue;
            }

            if (enemy.type == EnemyType::WaddleDee) {
                // Waddle Dee 是一般敵人，會被星星彈打掉。
                enemy.active = false;
            }

            // Gordo 不能被消滅，所以星星彈碰到 Gordo 只會自己消失。
            projectile.active = false;
            break;
        }
    }
}

// 更新水平 camera。
// Kirby 往右走時 camera 會跟著移動，但不會超出關卡最左或最右邊。
void MainWindow::updateCamera()
{
    // 讓 Kirby 盡量保持在畫面中間，所以鏡頭目標是 Kirby 中心點減掉半個螢幕。
    const float targetCameraX = m_kirbyX + kKirbyWidth / 2.0f - kScreenWidth / 2.0f;
    const float maxCameraX = static_cast<float>(currentStage().width - kScreenWidth);

    // cameraX 必須限制在 0 到 stageWidth - screenWidth。
    m_cameraX = clampFloat(targetCameraX, 0.0f, maxCameraX);
}

// Qt 需要重畫視窗時會呼叫。
// 依照目前 scene 決定畫 Start Menu、Stage、Clear 或 Game Over 畫面。
void MainWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

    if (m_scene == Scene::StartMenu) {
        drawStartMenu(painter);
    } else if (m_scene == Scene::Clear) {
        drawClear(painter);
    } else if (m_scene == Scene::GameOver) {
        drawGameOver(painter);
    } else {
        drawStage(painter);
    }
}

// 繪製開始畫面。
// painter 是 Qt 提供的畫筆物件，所有圖片和文字都透過它畫到視窗上。
void MainWindow::drawStartMenu(QPainter &painter)
{
    if (!m_startBackground.isNull()) {
        painter.drawPixmap(rect(), m_startBackground);
    } else {
        painter.fillRect(rect(), QColor(25, 35, 70));
    }

    // 加一層半透明黑色，讓標題和提示更清楚。
    painter.fillRect(rect(), QColor(0, 0, 0, 80));

    QFont titleFont(QStringLiteral("Georgia"), 86, QFont::Bold);
    painter.setFont(titleFont);
    painter.setPen(QPen(QColor(255, 245, 245)));

    const QString title = QStringLiteral("Kirby") + QChar(0x2019) + QStringLiteral("s Adventure");
    painter.drawText(QRect(0, 300, width(), 120), Qt::AlignCenter, title);

    QFont promptFont(QStringLiteral("Georgia"), 42, QFont::Bold);
    painter.setFont(promptFont);
    painter.setPen(QPen(QColor(255, 235, 110)));
    painter.drawText(QRect(0, 520, width(), 80), Qt::AlignCenter,
                     QStringLiteral("Press Enter to Start"));

    QFont helpFont(QStringLiteral("Georgia"), 28);
    painter.setFont(helpFont);
    painter.setPen(QPen(QColor(240, 240, 240)));
    painter.drawText(QRect(0, 640, width(), 60), Qt::AlignCenter,
                     QStringLiteral("Move: Left / Right    Jump: Up or Space"));
}

// 繪製目前關卡畫面。
// 這裡會依序畫背景、地板、場景物件、提示文字，最後畫 Kirby。
void MainWindow::drawStage(QPainter &painter)
{
    if (!m_stageBackdrop.isNull()) {
        painter.drawPixmap(rect(), m_stageBackdrop);
    } else {
        painter.fillRect(rect(), QColor(132, 218, 255));
    }

    drawStageFrames(painter);
    drawGround(painter);
    drawSceneObjects(painter);
    drawInhaleArea(painter);

    QFont stageFont(QStringLiteral("Georgia"), 30, QFont::Bold);
    painter.setFont(stageFont);
    painter.setPen(QPen(QColor(255, 255, 255)));
    painter.drawText(QRect(35, 25, 700, 50), Qt::AlignLeft | Qt::AlignVCenter,
                     m_scene == Scene::Stage1 ? QStringLiteral("Stage 1") : QStringLiteral("Stage 2"));

    QFont helpFont(QStringLiteral("Georgia"), 22);
    painter.setFont(helpFont);
    painter.drawText(QRect(35, 75, 1350, 40), Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("Move: Left/Right or A/D    Down: Squat/Swallow    Up: Jump/Hover/Door    X: Inhale/Spit/Exhale"));

    drawHud(painter);

    const bool isSquatting = m_downPressed && m_onGround && !m_isHovering;
    const QPixmap &kirbyPixmap = currentKirbyPixmap(isSquatting);

    // 所有 Kirby 動畫都畫在固定目標矩形內，collision rect 仍由 kirbyRect() 決定。
    // 蹲下只改變顯示高度，不改變碰撞位置，避免切圖造成卡牆或穿地板。
    const int drawHeight = isSquatting ? 72 : kKirbyHeight;
    const int kirbyBottom = static_cast<int>(m_kirbyY + kKirbyHeight);
    const int drawY = isSquatting ? kirbyBottom - drawHeight : static_cast<int>(m_kirbyY);
    const QRect kirbyTarget(static_cast<int>(m_kirbyX - m_cameraX),
                            drawY,
                            kKirbyWidth,
                            drawHeight);

    if (!kirbyPixmap.isNull()) {
        painter.drawPixmap(kirbyTarget, kirbyPixmap);
    } else {
        painter.setBrush(QColor(255, 130, 180));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(kirbyTarget);
    }
}

// 繪製過關畫面。
// Kirby 在 Stage 2 碰到 Goal 並按 Up 後會切換到這個畫面。
void MainWindow::drawClear(QPainter &painter)
{
    painter.fillRect(rect(), QColor(20, 70, 95));

    QFont titleFont(QStringLiteral("Georgia"), 96, QFont::Bold);
    painter.setFont(titleFont);
    painter.setPen(QPen(QColor(255, 245, 140)));
    painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("You Win!"));

    QFont subFont(QStringLiteral("Georgia"), 38, QFont::Bold);
    painter.setFont(subFont);
    painter.setPen(QPen(QColor(255, 255, 255)));
    painter.drawText(QRect(0, 650, width(), 80), Qt::AlignCenter, QStringLiteral("Clear!"));
}

// 繪製 Game Over 畫面。
// lives 歸零時會切到這裡；此時 updateGame() 不會再更新 Kirby 移動。
void MainWindow::drawGameOver(QPainter &painter)
{
    // Game Over 的選單文字與手指游標都已經畫在老師提供的兩張圖裡。
    // 滑鼠移到 Continue / Quit 時，m_gameOverSelectQuit 會切換，這裡就改畫對應圖片。
    const QPixmap &selectedImage = m_gameOverSelectQuit
            ? m_gameOverQuitImage
            : m_gameOverContinueImage;
    const QPixmap &fallbackImage = m_gameOverSelectQuit
            ? m_gameOverContinueImage
            : m_gameOverQuitImage;
    const QPixmap &gameOverImage = !selectedImage.isNull()
            ? selectedImage
            : fallbackImage;
    const bool usingFallbackImage = selectedImage.isNull() && !fallbackImage.isNull();

    if (!gameOverImage.isNull()) {
        // 等比例縮放置中，避免圖片因視窗比例不同而被拉伸變形。
        const QRect targetRect = gameOverImageRect();
        painter.drawPixmap(targetRect, gameOverImage);

        if (usingFallbackImage) {
            // 如果只有一張 Game Over 圖，就用小字標示目前選項，讓 Up / Down 切換仍看得出效果。
            painter.setPen(QPen(QColor(255, 240, 80)));
            painter.setFont(QFont(QStringLiteral("Georgia"), 30, QFont::Bold));
            painter.drawText(QRect(0, height() - 120, width(), 60),
                             Qt::AlignCenter,
                             m_gameOverSelectQuit ? QStringLiteral("> Quit") : QStringLiteral("> Continue"));
        }
        return;
    }

    // 正常情況 qrc 內會有兩張 Game Over 圖；如果素材遺失，只保留黑底，不再另外畫選單文字。
    painter.fillRect(rect(), QColor(0, 0, 0));
}

// 繪製 HUD 生命資訊。
// 每次畫 Stage 時呼叫，讓玩家知道目前剩幾條命和多少 HP。
void MainWindow::drawHud(QPainter &painter)
{
    painter.save();

    // HUD 放在地板下方，避免擋住 Kirby 的主要操作區。
    const int hudY = kScreenHeight - 125;

    painter.setBrush(QColor(0, 0, 0, 120));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(QRectF(35, hudY, 610, 88), 16, 16);

    QFont hudFont(QStringLiteral("Georgia"), 28, QFont::Bold);
    painter.setFont(hudFont);
    painter.setPen(QPen(QColor(255, 255, 255)));

    // lives 使用 Kirby 小頭圖示加上 x03 格式；如果 life.png 遺失，就用粉紅圓形 placeholder。
    const QRect lifeIconRect(62, hudY + 18, 54, 54);
    if (!m_lifeIcon.isNull()) {
        painter.drawPixmap(lifeIconRect, m_lifeIcon);
    } else {
        painter.setBrush(QColor(255, 140, 190));
        painter.setPen(QPen(QColor(255, 255, 255), 2));
        painter.drawEllipse(lifeIconRect);
        painter.drawText(lifeIconRect, Qt::AlignCenter, QStringLiteral("K"));
    }

    painter.setPen(QPen(QColor(255, 255, 255)));
    painter.drawText(QRect(125, hudY + 20, 110, 50),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("x %1").arg(m_lives, 2, 10, QChar('0')));

    painter.drawText(QRect(250, hudY + 20, 75, 50),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("HP"));

    // HP 用三格 icon 顯示；起始 x 刻意往右留空，避免紅色血條蓋住 HP 文字。
    for (int i = 0; i < kMaxHp; ++i) {
        const QRect hpRect(365 + i * 62, hudY + 20, 50, 50);
        const QPixmap &hpPixmap = i < m_hp ? m_hpFullIcon : m_hpEmptyIcon;
        if (!hpPixmap.isNull()) {
            painter.drawPixmap(hpRect, hpPixmap);
        } else {
            painter.setBrush(i < m_hp ? QColor(220, 40, 55) : QColor(80, 80, 90));
            painter.setPen(QPen(QColor(255, 255, 255), 2));
            painter.drawRoundedRect(hpRect, 8, 8);
        }
    }

    QString stateText = QStringLiteral("Normal");
    if (m_kirbyState == KirbyState::Inhaling) {
        stateText = QStringLiteral("Inhaling");
    } else if (m_kirbyState == KirbyState::Mouthful) {
        stateText = QStringLiteral("Mouthful");
    } else if (m_kirbyState == KirbyState::AbilityState) {
        stateText = QStringLiteral("Ability");
    }

    painter.setBrush(QColor(0, 0, 0, 110));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(QRectF(675, hudY, 445, 88), 16, 16);

    QFont stateFont(QStringLiteral("Georgia"), 24, QFont::Bold);
    painter.setFont(stateFont);
    painter.setPen(QPen(QColor(255, 255, 255)));
    painter.drawText(QRect(700, hudY + 16, 390, 35),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("State: %1").arg(stateText));

    if (m_invincibleFrames > 0) {
        painter.setPen(QPen(QColor(255, 240, 80)));
        painter.drawText(QRect(700, hudY + 48, 390, 30),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("Invincible"));
    }

    painter.restore();
}

// 繪製關卡背景 frames。
// worldX 是該 frame 在關卡中的世界座標，畫到螢幕時要減掉 m_cameraX。
void MainWindow::drawStageFrames(QPainter &painter)
{
    const int frameCount = currentStage().width / kScreenWidth;

    for (int frame = 0; frame < frameCount; ++frame) {
        const QPixmap *frameImage = nullptr;

        if (m_scene == Scene::Stage1) {
            if (frame == 0) {
                frameImage = &m_stage1Frame1;
            } else if (frame == 1) {
                frameImage = &m_stage1Frame2;
            } else {
                frameImage = &m_stage1Frame3;
            }
        } else {
            // Dataset 只有兩張 Stage 2 圖，先交替鋪成 5 個 frames。
            frameImage = (frame % 2 == 0) ? &m_stage2Frame1 : &m_stage2Frame2;
        }

        if (frameImage == nullptr || frameImage->isNull()) {
            continue;
        }

        const int worldX = frame * kScreenWidth;
        const QRect targetRect(static_cast<int>(worldX - m_cameraX),
                               kGroundTop - frameImage->height(),
                               kScreenWidth,
                               frameImage->height());
        painter.drawPixmap(targetRect, *frameImage);
    }
}

// 繪製地板與 hole。
// hole 先畫成黑色缺口，鋪地板時再跳過缺口位置，讓玩家能看出危險區。
void MainWindow::drawGround(QPainter &painter)
{
    // 先把 hole 畫成黑色缺口，讓玩家看得出哪裡會掉下去。
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0));
    for (const QRectF &hole : currentStage().holes) {
        painter.drawRect(QRectF(hole.left() - m_cameraX, hole.top(), hole.width(), hole.height()));
    }

    if (m_floorTile.isNull() || m_floorTile.width() <= 0) {
        return;
    }

    const int tileWidth = m_floorTile.width();
    const int firstTileX = static_cast<int>(m_cameraX) / tileWidth * tileWidth;
    const int lastVisibleX = static_cast<int>(m_cameraX) + kScreenWidth + tileWidth;

    for (int worldX = firstTileX; worldX <= lastVisibleX; worldX += tileWidth) {
        const QRectF tileRect(worldX, kGroundTop, tileWidth, m_floorTile.height());
        bool tileIsHole = false;

        for (const QRectF &hole : currentStage().holes) {
            if (tileRect.intersects(hole)) {
                tileIsHole = true;
                break;
            }
        }

        if (!tileIsHole) {
            painter.drawPixmap(static_cast<int>(worldX - m_cameraX), kGroundTop, m_floorTile);
        }
    }
}

// 繪製所有場景物件。
// 包含平台、Block、道具、敵人、Stage 1 的 Portal，以及 Stage 2 的 Goal。
void MainWindow::drawSceneObjects(QPainter &painter)
{
    for (const QRectF &platform : currentStage().platforms) {
        drawTiledPixmap(painter, m_floorTile, platform);
    }

    for (const QRectF &block : currentStage().blocks) {
        if (!m_blockTile.isNull()) {
            painter.drawPixmap(QRect(static_cast<int>(block.left() - m_cameraX),
                                     static_cast<int>(block.top()),
                                     static_cast<int>(block.width()),
                                     static_cast<int>(block.height())),
                               m_blockTile);
        } else {
            painter.fillRect(QRectF(block.left() - m_cameraX, block.top(), block.width(), block.height()),
                             QColor(180, 180, 180));
        }
    }

    drawItems(painter);
    drawEnemies(painter);
    drawProjectiles(painter);

    if (m_scene == Scene::Stage1 && !m_stage1.portal.isNull()) {
        painter.drawPixmap(QRect(static_cast<int>(m_stage1.portal.left() - m_cameraX),
                                 static_cast<int>(m_stage1.portal.top()),
                                 static_cast<int>(m_stage1.portal.width()),
                                 static_cast<int>(m_stage1.portal.height())),
                           m_portalImage);
    }

    if (m_scene == Scene::Stage2 && !m_stage2.goal.isNull()) {
        painter.drawPixmap(QRect(static_cast<int>(m_stage2.goal.left() - m_cameraX),
                                 static_cast<int>(m_stage2.goal.top()),
                                 static_cast<int>(m_stage2.goal.width()),
                                 static_cast<int>(m_stage2.goal.height())),
                           m_goalImage);
    }
}

// 繪製尚未收集的 Stage 1 道具。
// cameraX 代表鏡頭左邊的位置，所以畫到螢幕上時要把世界座標扣掉 cameraX。
void MainWindow::drawItems(QPainter &painter)
{
    if (m_scene != Scene::Stage1) {
        return;
    }

    for (const Item &item : m_items) {
        if (item.collected) {
            continue;
        }

        const QRect target(static_cast<int>(item.rect.left() - m_cameraX),
                           static_cast<int>(item.rect.top()),
                           static_cast<int>(item.rect.width()),
                           static_cast<int>(item.rect.height()));

        const QPixmap &itemPixmap = item.type == ItemType::MaximTomato
                ? m_maximTomatoImage
                : m_oneUpImage;

        if (!itemPixmap.isNull()) {
            painter.drawPixmap(target, itemPixmap);
        } else {
            // 如果圖片載入失敗，仍用簡單圖形顯示，方便 Demo 時看出道具位置。
            painter.setBrush(item.type == ItemType::MaximTomato ? QColor(230, 40, 60) : QColor(255, 230, 60));
            painter.setPen(QPen(QColor(255, 255, 255), 3));
            painter.drawEllipse(target);
            painter.drawText(target, Qt::AlignCenter,
                             item.type == ItemType::MaximTomato ? QStringLiteral("HP") : QStringLiteral("1UP"));
        }
    }
}

// 繪製 Stage 1 的敵人。
// Waddle Dee 用一般敵人圖片；Gordo 用刺球圖片，代表不可被消滅、只能避開的障礙。
void MainWindow::drawEnemies(QPainter &painter)
{
    if (m_scene != Scene::Stage1) {
        return;
    }

    for (const Enemy &enemy : m_enemies) {
        if (!enemy.active) {
            continue;
        }

        const QRect target(static_cast<int>(enemy.rect.left() - m_cameraX),
                           static_cast<int>(enemy.rect.top()),
                           static_cast<int>(enemy.rect.width()),
                           static_cast<int>(enemy.rect.height()));

        const QPixmap &enemyPixmap = enemy.type == EnemyType::WaddleDee
                ? m_waddleDeeImage
                : m_gordoImage;

        if (!enemyPixmap.isNull()) {
            painter.drawPixmap(target, enemyPixmap);
        } else {
            // 圖片載入失敗時仍畫出替代圖形，方便 Demo 時確認敵人碰撞位置。
            painter.setPen(QPen(QColor(255, 255, 255), 3));
            if (enemy.type == EnemyType::WaddleDee) {
                painter.setBrush(QColor(205, 95, 55));
                painter.drawRoundedRect(target, 12, 12);
                painter.drawText(target, Qt::AlignCenter, QStringLiteral("W"));
            } else {
                painter.setBrush(QColor(90, 90, 105));
                painter.drawEllipse(target);
                painter.drawText(target, Qt::AlignCenter, QStringLiteral("G"));
            }
        }
    }
}

// 繪製吸力範圍。
// 只有 Inhaling 狀態才會顯示，矩形在 Kirby 面向的前方。
void MainWindow::drawInhaleArea(QPainter &painter)
{
    if (m_scene != Scene::Stage1 || m_kirbyState != KirbyState::Inhaling) {
        return;
    }

    const QRectF suctionRect = inhaleRect();
    painter.save();
    painter.setBrush(QColor(120, 210, 255, 85));
    painter.setPen(QPen(QColor(220, 250, 255), 3, Qt::DashLine));
    painter.drawRoundedRect(QRectF(suctionRect.left() - m_cameraX,
                                   suctionRect.top(),
                                   suctionRect.width(),
                                   suctionRect.height()),
                            18,
                            18);
    painter.restore();
}

// 繪製星星彈。
// 星星彈是 Mouthful 狀態按 X 吐出的 projectile，會往 Kirby 面向方向飛。
void MainWindow::drawProjectiles(QPainter &painter)
{
    if (m_scene != Scene::Stage1) {
        return;
    }

    painter.save();

    for (const Projectile &projectile : m_projectiles) {
        if (!projectile.active) {
            continue;
        }

        const QRectF target(projectile.rect.left() - m_cameraX,
                            projectile.rect.top(),
                            projectile.rect.width(),
                            projectile.rect.height());

        if (!m_spitStarImage.isNull()) {
            // Dataset 有星星彈素材時直接使用圖片；碰撞邏輯仍然使用 projectile.rect。
            painter.drawPixmap(target.toRect(), m_spitStarImage);
        } else {
            // placeholder：素材不存在時用五角星補上，避免星星彈看起來像普通圓球。
            drawStarPlaceholder(painter, target);
        }
    }

    painter.restore();
}

// 用 QPainter 畫黃色五角星 placeholder。
// 只有在星星彈圖片沒有載入時才會使用，純粹影響外觀，不改變 projectile 碰撞範圍。
void MainWindow::drawStarPlaceholder(QPainter &painter, const QRectF &target) const
{
    painter.save();
    painter.setBrush(QColor(255, 230, 60));
    painter.setPen(QPen(QColor(255, 255, 255), 3));
    painter.drawPolygon(makeStarPolygon(target));
    painter.restore();
}

// 將一張圖重複鋪在指定的世界座標矩形上。
// 目前主要用來畫平台；worldRect 是關卡座標，實際畫面座標要扣掉 cameraX。
void MainWindow::drawTiledPixmap(QPainter &painter, const QPixmap &pixmap, const QRectF &worldRect)
{
    if (pixmap.isNull() || pixmap.width() <= 0) {
        painter.fillRect(QRectF(worldRect.left() - m_cameraX, worldRect.top(), worldRect.width(), worldRect.height()),
                         QColor(160, 110, 60));
        return;
    }

    for (int x = static_cast<int>(worldRect.left()); x < worldRect.right(); x += pixmap.width()) {
        const int drawWidth = std::min(pixmap.width(), static_cast<int>(worldRect.right()) - x);
        const QRect target(static_cast<int>(x - m_cameraX),
                           static_cast<int>(worldRect.top()),
                           drawWidth,
                           static_cast<int>(worldRect.height()));
        painter.drawPixmap(target, pixmap, QRect(0, 0, drawWidth, pixmap.height()));
    }
}

// 重設整場遊戲。
// Game Over 畫面按 C 時呼叫；會回到 Start Menu，並把 lives / HP 全部恢復。
void MainWindow::resetGame()
{
    m_scene = Scene::StartMenu;
    m_lives = kInitialLives;
    m_hp = kMaxHp;
    m_invincibleFrames = 0;
    m_gameOverSelectQuit = false;
    setupItems();
    setupEnemies();
    resetStageStart();
    unsetCursor();
    setFocus(Qt::OtherFocusReason);
    update();
}

// 處理 Game Over 畫面的 Continue / Quit 選單。
// 這個函式會在一般遊戲操作之前被呼叫，避免方向鍵被拿去控制 Kirby。
bool MainWindow::handleGameOverKey(int key)
{
    if (m_scene != Scene::GameOver) {
        return false;
    }

    switch (key) {
    case Qt::Key_Up:
    case Qt::Key_Left:
        // 選到 Continue 時會顯示 game_over_continue 圖片。
        m_gameOverSelectQuit = false;
        update();
        return true;
    case Qt::Key_Down:
    case Qt::Key_Right:
        // 選到 Quit 時會顯示 game_over_quit 圖片。
        m_gameOverSelectQuit = true;
        update();
        return true;
    case Qt::Key_C:
        // 快捷鍵：不管目前游標在哪裡，C 都直接 Continue。
        resetGame();
        return true;
    case Qt::Key_Q:
        // 快捷鍵：Q 直接關閉視窗，等同選擇 Quit。
        close();
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        // Enter 會執行目前選項：Continue 回 Start Menu，Quit 關閉程式。
        activateGameOverSelection();
        return true;
    default:
        return false;
    }
}

// 計算 Game Over 圖片實際顯示的位置。
// 目前素材剛好是 1620x1080；若未來全螢幕造成左右留黑邊，hitbox 也會跟著圖片位置換算。
QRect MainWindow::gameOverImageRect() const
{
    const QPixmap &gameOverImage = !m_gameOverContinueImage.isNull()
            ? m_gameOverContinueImage
            : m_gameOverQuitImage;

    if (gameOverImage.isNull()) {
        return rect();
    }

    const QSize scaledSize = gameOverImage.size().scaled(size(), Qt::KeepAspectRatio);
    return QRect((width() - scaledSize.width()) / 2,
                 (height() - scaledSize.height()) / 2,
                 scaledSize.width(),
                 scaledSize.height());
}

// Continue 按鈕在原始 1620x1080 Game Over 圖上的大約範圍。
// 用較寬鬆的矩形讓玩家滑到文字或框線附近都能選到 Continue。
QRect MainWindow::gameOverContinueButtonRect() const
{
    const QRect imageRect = gameOverImageRect();
    const QRectF sourceRect(900.0, 510.0, 700.0, 130.0);
    const qreal scaleX = imageRect.width() / 1620.0;
    const qreal scaleY = imageRect.height() / 1080.0;

    return QRect(imageRect.left() + static_cast<int>(sourceRect.left() * scaleX),
                 imageRect.top() + static_cast<int>(sourceRect.top() * scaleY),
                 static_cast<int>(sourceRect.width() * scaleX),
                 static_cast<int>(sourceRect.height() * scaleY));
}

// Quit 按鈕在原始 1620x1080 Game Over 圖上的大約範圍。
// 滑鼠移到這個區域時會切換成 game_over_quit.png。
QRect MainWindow::gameOverQuitButtonRect() const
{
    const QRect imageRect = gameOverImageRect();
    const QRectF sourceRect(900.0, 650.0, 700.0, 155.0);
    const qreal scaleX = imageRect.width() / 1620.0;
    const qreal scaleY = imageRect.height() / 1080.0;

    return QRect(imageRect.left() + static_cast<int>(sourceRect.left() * scaleX),
                 imageRect.top() + static_cast<int>(sourceRect.top() * scaleY),
                 static_cast<int>(sourceRect.width() * scaleX),
                 static_cast<int>(sourceRect.height() * scaleY));
}

// 依滑鼠目前位置更新 Game Over 選項。
// 回傳 true 代表滑鼠在 Continue 或 Quit 上，可以接受點擊。
bool MainWindow::updateGameOverSelectionFromMouse(const QPoint &position)
{
    if (m_scene != Scene::GameOver) {
        unsetCursor();
        return false;
    }

    const bool oldSelectQuit = m_gameOverSelectQuit;

    if (gameOverContinueButtonRect().contains(position)) {
        m_gameOverSelectQuit = false;
    } else if (gameOverQuitButtonRect().contains(position)) {
        m_gameOverSelectQuit = true;
    } else {
        unsetCursor();
        return false;
    }

    setCursor(Qt::PointingHandCursor);
    if (oldSelectQuit != m_gameOverSelectQuit) {
        update();
    }

    return true;
}

// 執行 Game Over 目前選項。
// Continue 會重設遊戲並回 Start Menu；Quit 會直接關閉視窗。
void MainWindow::activateGameOverSelection()
{
    if (m_gameOverSelectQuit) {
        close();
    } else {
        resetGame();
    }
}

// 扣掉一條 life。
// 掉進 hole、掉出畫面下方，或 HP 歸零時會呼叫；若 lives 歸零就進入 Game Over。
void MainWindow::loseLife()
{
    --m_lives;
    m_hp = kMaxHp;

    if (m_lives <= 0) {
        m_lives = 0;
        m_invincibleFrames = 0;
        m_scene = Scene::GameOver;
        m_gameOverSelectQuit = false;
        m_leftPressed = false;
        m_rightPressed = false;
        m_upPressed = false;
        m_downPressed = false;
        m_isHovering = false;
        m_kirbyState = KirbyState::Normal;
        m_hasMouthfulEnemy = false;
        m_exhaleEffectFrames = 0;
        m_spitAnimationFrames = 0;
        m_animationTimer = 0;
        m_projectiles.clear();
        setFocus(Qt::OtherFocusReason);
        update();
        return;
    }

    resetStageStart();
    m_invincibleFrames = kInvincibleFrames;
}

// Kirby 受傷時呼叫。
// Waddle Dee 或 Gordo 碰到 Kirby 時會呼叫；無敵中不會再次扣 HP。
void MainWindow::damageKirby()
{
    if (m_scene != Scene::Stage1 && m_scene != Scene::Stage2) {
        return;
    }

    if (m_invincibleFrames > 0) {
        return;
    }

    --m_hp;
    m_invincibleFrames = kInvincibleFrames;

    if (m_hp <= 0) {
        loseLife();
    }
}

// 開始吸入。
// 只有 Normal 狀態可以進入 Inhaling；Mouthful 時嘴裡已經有敵人，不能再吸入。
void MainWindow::startInhaling()
{
    if (m_scene != Scene::Stage1 || m_kirbyState != KirbyState::Normal) {
        return;
    }

    m_kirbyState = KirbyState::Inhaling;
}

// 停止吸入。
// 玩家放開 X 時呼叫；如果還沒吸到敵人，就回到 Normal 狀態。
void MainWindow::stopInhaling()
{
    if (m_kirbyState == KirbyState::Inhaling) {
        m_kirbyState = KirbyState::Normal;
    }
}

// 吞下嘴裡敵人。
// 目前 Stage 1 只有 Waddle Dee 可被吸入，而且沒有複製能力，所以吞下後只回到 Normal。
void MainWindow::swallowMouthfulEnemy()
{
    if (m_kirbyState != KirbyState::Mouthful || !m_hasMouthfulEnemy) {
        return;
    }

    if (m_mouthfulEnemyType == EnemyType::WaddleDee) {
        // Waddle Dee 是一般敵人，不會提供 Fire 或 Spark ability。
        m_kirbyState = KirbyState::Normal;
        m_hasMouthfulEnemy = false;
        m_downPressed = false;
    }
}

// 吐出星星彈。
// Mouthful 狀態按 X 時呼叫；星星彈會依 Kirby 面向方向水平飛出。
void MainWindow::spitStar()
{
    if (m_scene != Scene::Stage1 || m_kirbyState != KirbyState::Mouthful || !m_hasMouthfulEnemy) {
        return;
    }

    const float starX = m_faceRight
            ? m_kirbyX + kKirbyWidth
            : m_kirbyX - kSpitStarSize;
    const float starY = m_kirbyY + kKirbyHeight / 2.0f - kSpitStarSize / 2.0f;
    const float velocityX = m_faceRight ? kSpitStarSpeed : -kSpitStarSpeed;

    m_projectiles << Projectile{QRectF(starX, starY, kSpitStarSize, kSpitStarSize),
                                velocityX,
                                true};
    // Spit 動畫只是 Kirby 本體的短暫視覺效果，不會改變星星彈 projectile 的碰撞判定。
    m_spitAnimationFrames = kSpitAnimationFrames;
    m_kirbyState = KirbyState::Normal;
    m_hasMouthfulEnemy = false;
}

// 切換到 Stage 2。
// Kirby 接觸 Stage 1 最後的 Portal 並按 Up 時呼叫。
void MainWindow::switchToStage2()
{
    m_scene = Scene::Stage2;
    resetStageStart();
}

// 切換到 Clear 畫面。
// Kirby 接觸 Stage 2 最後的 Goal 並按 Up 時呼叫。
void MainWindow::switchToClear()
{
    m_scene = Scene::Clear;
    m_leftPressed = false;
    m_rightPressed = false;
    m_upPressed = false;
    m_downPressed = false;
    m_isHovering = false;
    m_kirbyState = KirbyState::Normal;
    m_hasMouthfulEnemy = false;
    m_exhaleEffectFrames = 0;
    m_spitAnimationFrames = 0;
    m_animationTimer = 0;
    m_projectiles.clear();
}

// 取得目前關卡資料。
// Stage 相關邏輯都透過這個函式取得 width、platform、block、hole 等資料。
const MainWindow::StageLayout &MainWindow::currentStage() const
{
    return m_scene == Scene::Stage2 ? m_stage2 : m_stage1;
}

// 回傳 Kirby 的碰撞矩形。
// 注意：蹲下只影響顯示圖片高度，不改變這個碰撞矩形。
QRectF MainWindow::kirbyRect() const
{
    return QRectF(m_kirbyX, m_kirbyY, kKirbyWidth, kKirbyHeight);
}

// 回傳 Kirby 前方的吸力範圍。
// 面向右邊時範圍在右側，面向左邊時範圍在左側。
QRectF MainWindow::inhaleRect() const
{
    const float inhaleX = m_faceRight
            ? m_kirbyX + kKirbyWidth
            : m_kirbyX - kInhaleWidth;
    const float inhaleY = m_kirbyY + kKirbyHeight / 2.0f - kInhaleHeight / 2.0f;
    return QRectF(inhaleX, inhaleY, kInhaleWidth, kInhaleHeight);
}

// 根據 Kirby 目前狀態選圖片。
// 只改變繪圖用 QPixmap，不改變 kirbyRect() 的碰撞大小，避免動畫切圖造成卡牆或掉地板。
// 優先順序是吐星星 Spit、吐氣特效、吸入、Mouthful、蹲下、飛行、跳躍、跑步、站立。
const QPixmap &MainWindow::currentKirbyPixmap(bool isSquatting) const
{
    if (m_spitAnimationFrames > 0) {
        // Spit 圖片只在吐出星星後短暫顯示；如果素材讀不到，才退回吸入張嘴圖當 placeholder。
        const QPixmap &pixmap = m_faceRight ? m_kirbySpitRight : m_kirbySpitLeft;
        if (!pixmap.isNull()) {
            return pixmap;
        }

        const QPixmap &fallbackPixmap = m_faceRight ? m_kirbyInhaleRight : m_kirbyInhaleLeft;
        if (!fallbackPixmap.isNull()) {
            return fallbackPixmap;
        }
    }

    if (m_exhaleEffectFrames > 0) {
        const QPixmap &pixmap = m_faceRight ? m_kirbyExhaleRight : m_kirbyExhaleLeft;
        if (!pixmap.isNull()) {
            return pixmap;
        }
    }

    if (m_kirbyState == KirbyState::Inhaling) {
        const QPixmap &pixmap = m_faceRight ? m_kirbyInhaleRight : m_kirbyInhaleLeft;
        if (!pixmap.isNull()) {
            return pixmap;
        }
    }

    if (m_kirbyState == KirbyState::Mouthful) {
        // Dataset 沒有專用 mouthful 圖，先用鼓起的飛行圖當 placeholder，Demo 時可看出嘴裡有敵人。
        const QPixmap &pixmap = m_faceRight ? m_kirbyMouthfulRight : m_kirbyMouthfulLeft;
        if (!pixmap.isNull()) {
            return pixmap;
        }
    }

    if (isSquatting) {
        return m_faceRight ? m_kirbySquatRight : m_kirbySquatLeft;
    }

    if (m_isHovering) {
        // Hover 依面向在 fly_1 / fly_2 之間循環，讓飛行狀態看起來有動畫。
        const int frame = (m_animationTimer / kHoverAnimationTicks) % 2;
        return m_faceRight
                ? animationFrame(m_kirbyHoverRightFrames, frame, m_kirbyHoverRight)
                : animationFrame(m_kirbyHoverLeftFrames, frame, m_kirbyHoverLeft);
    }

    if (!m_onGround) {
        // 跳躍沒有 L/R 版本，所以用垂直速度挑三張共用圖：上升、最高點附近、下落。
        int frame = 0;
        if (m_kirbyVelocityY > 5.0f) {
            frame = 2;
        } else if (m_kirbyVelocityY > -5.0f) {
            frame = 1;
        }
        return animationFrame(m_kirbyJumpFrames, frame, m_kirbyJump);
    }

    if (m_leftPressed != m_rightPressed) {
        // 地面左右移動時播放 run_1 到 run_4；每 8 個 tick 換一張，速度比較接近遊戲角色跑步。
        const int frame = (m_animationTimer / kRunAnimationTicks) % 4;
        return m_faceRight
                ? animationFrame(m_kirbyRunRightFrames, frame, m_kirbyRunRight)
                : animationFrame(m_kirbyRunLeftFrames, frame, m_kirbyRunLeft);
    }

    return m_faceRight ? m_kirbyStopRight : m_kirbyStopLeft;
}

// 判斷兩個矩形在水平方向是否重疊。
// 平台落地與 Block 落地都需要先確認 x 方向有重疊。
bool MainWindow::horizontalOverlap(const QRectF &a, const QRectF &b) const
{
    return a.right() > b.left() && a.left() < b.right();
}

// 判斷 Kirby 是否碰到 Portal 或 Goal。
// objectRect 是 Portal/Goal 的矩形，稍微放大 Kirby 判定讓按 Up 比較容易成功。
bool MainWindow::touchesInteractionObject(const QRectF &objectRect) const
{
    if (objectRect.isNull()) {
        return false;
    }

    // 稍微放大判定，讓按 Up 進門不要太難觸發。
    return kirbyRect().adjusted(-10, -10, 10, 10).intersects(objectRect);
}

// 判斷指定矩形是否碰到任一 Block。
// 目前用於 Kirby 左右移動時，確認是否撞到實心障礙物。
bool MainWindow::collidesWithAnyBlock(const QRectF &rect) const
{
    for (const QRectF &block : currentStage().blocks) {
        if (rect.intersects(block)) {
            return true;
        }
    }
    return false;
}

// 判斷 Kirby 腳底中心是否在 hole 裡。
// 只有腳底中心進入地板缺口，Kirby 才會掉下去。
bool MainWindow::isOverHole(const QRectF &rect) const
{
    const QPointF feetCenter(rect.center().x(), rect.bottom());

    for (const QRectF &hole : currentStage().holes) {
        if (hole.contains(feetCenter)) {
            return true;
        }
    }
    return false;
}

// 判斷 Kirby 腳下是否有支撐。
// 如果沒有地板、平台或 Block 支撐，Kirby 就要開始下落。
bool MainWindow::hasGroundSupport(const QRectF &rect) const
{
    const float footY = static_cast<float>(rect.bottom());

    if (std::fabs(footY - kGroundTop) <= 2.0f && !isOverHole(rect)) {
        return true;
    }

    for (const QRectF &platform : currentStage().platforms) {
        if (std::fabs(footY - platform.top()) <= 2.0f && horizontalOverlap(rect, platform)) {
            return true;
        }
    }

    for (const QRectF &block : currentStage().blocks) {
        if (std::fabs(footY - block.top()) <= 2.0f && horizontalOverlap(rect, block)) {
            return true;
        }
    }

    return false;
}

// 處理鍵盤按下事件。
// Enter 進入遊戲，方向鍵移動/蹲下/跳躍，Up 可進門或 Hover，X 會依 KirbyState 吸入/吐星星/吐氣。
// 暫時 Debug：H 扣 1 HP、L 扣 1 life，方便測試道具，之後可以移除。
// Game Over 時，C 會 Continue 回 Start Menu，Q 會 Quit 關閉程式。
void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (handleGameOverKey(event->key())) {
        event->accept();
        return;
    }

    // 長按按鍵會產生重複事件；這裡忽略，避免跳躍被連續觸發。
    if (event->isAutoRepeat()) {
        QMainWindow::keyPressEvent(event);
        return;
    }

    if (event->key() == Qt::Key_F11) {
        // F11 只切換視窗顯示模式，不會改變 Kirby 的位置、關卡或遊戲狀態。
        if (isFullScreen()) {
            showNormal();
        } else {
            showFullScreen();
        }
        return;
    }

    switch (event->key()) {
    case Qt::Key_H:
        if (m_scene == Scene::Stage1 || m_scene == Scene::Stage2) {
            // Debug 測試用：直接扣 1 HP，不受無敵時間影響，方便測 Maxim Tomato 補滿 HP。
            --m_hp;
            if (m_hp <= 0) {
                loseLife();
            }
        }
        break;
    case Qt::Key_L:
        if (m_scene == Scene::Stage1 || m_scene == Scene::Stage2) {
            // Debug 測試用：直接扣 1 life，方便測 1UP 是否能把 lives 補回上限 3。
            loseLife();
        }
        break;
    case Qt::Key_C:
        if (m_scene == Scene::GameOver) {
            // Continue：回到 Start Menu，並重設 lives、HP 和 Kirby 位置。
            resetGame();
        }
        break;
    case Qt::Key_Q:
        if (m_scene == Scene::GameOver) {
            // Quit：關閉程式。
            close();
        }
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (m_scene == Scene::StartMenu) {
            m_scene = Scene::Stage1;
            resetStageStart();
        }
        break;
    case Qt::Key_Left:
    case Qt::Key_A:
        m_leftPressed = true;
        break;
    case Qt::Key_Right:
    case Qt::Key_D:
        m_rightPressed = true;
        break;
    case Qt::Key_Down:
    case Qt::Key_S:
        if (m_kirbyState == KirbyState::Mouthful) {
            // Mouthful 狀態按 Down 代表吞下嘴裡敵人，不做蹲下動作。
            swallowMouthfulEnemy();
            break;
        }
        m_downPressed = true;
        break;
    case Qt::Key_Up:
        m_upPressed = true;

        if (m_scene == Scene::Stage1 && touchesInteractionObject(m_stage1.portal)) {
            switchToStage2();
            return;
        }

        if (m_scene == Scene::Stage2 && touchesInteractionObject(m_stage2.goal)) {
            switchToClear();
            return;
        }

        if ((m_scene == Scene::Stage1 || m_scene == Scene::Stage2)
                && m_isHovering
                && m_kirbyState != KirbyState::Mouthful) {
            // Hover 狀態即使在地面上，按 Up 也只是往上推升，不會變回普通跳躍。
            m_onGround = false;
            m_canStartHover = false;
            break;
        }

        if ((m_scene == Scene::Stage1 || m_scene == Scene::Stage2)
                && m_onGround
                && !m_isHovering
                && m_kirbyState != KirbyState::Mouthful) {
            // 第一次按 Up 是跳躍；必須在空中再次按 Up 才會進入 Hover。
            m_kirbyVelocityY = kJumpSpeed;
            m_onGround = false;
            m_isHovering = false;
            m_canStartHover = false;
        } else if ((m_scene == Scene::Stage1 || m_scene == Scene::Stage2)
                   && !m_onGround
                   && !m_isHovering
                   && m_canStartHover
                   && m_kirbyState != KirbyState::Mouthful) {
            // 空中再次按 Up，才切換成飛行 Hover。
            m_isHovering = true;
            m_kirbyVelocityY = 0.0f;
        }
        break;
    case Qt::Key_Space:
        if ((m_scene == Scene::Stage1 || m_scene == Scene::Stage2)
                && m_onGround
                && !m_isHovering
                && m_kirbyState != KirbyState::Mouthful) {
            // 保留第一階段的 Space 跳躍；Space 不會直接啟動飛行。
            m_kirbyVelocityY = kJumpSpeed;
            m_onGround = false;
            m_isHovering = false;
            m_canStartHover = true;
        }
        break;
    case Qt::Key_X:
        if (m_kirbyState == KirbyState::Mouthful) {
            // Mouthful 狀態按 X 代表吐出星星彈。
            spitStar();
        } else if ((m_scene == Scene::Stage1 || m_scene == Scene::Stage2) && m_isHovering) {
            // Hover 只能透過 X 吐氣 Exhale 解除；落地、平台碰撞或 Space 都不會自動取消。
            m_isHovering = false;
            m_canStartHover = false;
            // 只新增短暫吐氣視覺，不改變原本 Hover 解除與下落邏輯。
            m_exhaleEffectFrames = kExhaleEffectFrames;
            m_kirbyVelocityY = std::max(m_kirbyVelocityY, 2.0f);
        } else if (m_kirbyState == KirbyState::Normal) {
            // Normal 狀態按住 X 代表吸入，放開 X 會停止吸入。
            startInhaling();
        }
        break;
    default:
        QMainWindow::keyPressEvent(event);
        break;
    }
}

// 處理鍵盤放開事件。
// 放開左右鍵會停止移動；放開 Up 後，下一次空中按 Up 才能進入 Hover。
void MainWindow::keyReleaseEvent(QKeyEvent *event)
{
    if (handleGameOverKey(event->key())) {
        event->accept();
        return;
    }

    if (event->isAutoRepeat()) {
        QMainWindow::keyReleaseEvent(event);
        return;
    }

    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_A:
        m_leftPressed = false;
        break;
    case Qt::Key_Right:
    case Qt::Key_D:
        m_rightPressed = false;
        break;
    case Qt::Key_Down:
    case Qt::Key_S:
        m_downPressed = false;
        break;
    case Qt::Key_Up:
        m_upPressed = false;

        // 放開 Up 後，下一次在空中按 Up 才可以進入 Hover。
        if (!m_onGround) {
            m_canStartHover = true;
        }
        break;
    case Qt::Key_X:
        // 放開 X 時，如果 Kirby 還在吸入，就停止吸入並回到 Normal。
        stopInhaling();
        break;
    default:
        QMainWindow::keyReleaseEvent(event);
        break;
    }
}

// Game Over 畫面滑鼠移動事件。
// 滑到 Continue / Quit 按鈕區域時，會切換成對應的 Game Over 圖片。
void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_scene == Scene::GameOver) {
        updateGameOverSelectionFromMouse(event->pos());
        event->accept();
        return;
    }

    unsetCursor();
    QMainWindow::mouseMoveEvent(event);
}

// Game Over 畫面滑鼠點擊事件。
// 左鍵點擊 Continue 會回 Start Menu；左鍵點擊 Quit 會呼叫 close() 關閉程式。
void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if (m_scene == Scene::GameOver && event->button() == Qt::LeftButton) {
        if (updateGameOverSelectionFromMouse(event->pos())) {
            activateGameOverSelection();
            event->accept();
            return;
        }
    }

    QMainWindow::mousePressEvent(event);
}
