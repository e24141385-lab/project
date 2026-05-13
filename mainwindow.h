#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QRectF>
#include <QTimer>
#include <QVector>

class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QPainter;

// MainWindow 是目前遊戲的主控制類別。
// 它負責接收鍵盤、更新 Kirby 狀態、處理碰撞與 camera，並把 Start / Stage / Clear 畫面畫出來。
class MainWindow : public QMainWindow
{
public:
    // 建立遊戲視窗，會設定視窗大小、載入素材、建立關卡資料並啟動遊戲迴圈。
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    // Qt 需要重畫視窗時會呼叫。這裡依照目前 scene 決定要畫開始畫面、關卡或 Clear 畫面。
    void paintEvent(QPaintEvent *event) override;

    // 玩家按下鍵盤時會呼叫。這裡處理 Enter、方向鍵、跳躍、飛行、進門與吐氣。
    // F11 可以切換全螢幕，避免 1620x1080 視窗底部被 Windows 工作列遮住。
    void keyPressEvent(QKeyEvent *event) override;

    // 玩家放開鍵盤時會呼叫。這裡停止左右移動、蹲下，並記錄 Up 是否已放開。
    void keyReleaseEvent(QKeyEvent *event) override;

    // Game Over 畫面用滑鼠移動切換 Continue / Quit 圖片。
    void mouseMoveEvent(QMouseEvent *event) override;

    // Game Over 畫面用滑鼠左鍵點擊目前滑到的選項。
    void mousePressEvent(QMouseEvent *event) override;

private:
    // 目前的遊戲畫面：開始畫面、兩個關卡、過關畫面。
    enum class Scene {
        StartMenu,
        Stage1,
        Stage2,
        Clear,
        GameOver
    };

    // KirbyState 管理 Kirby 目前動作狀態。
    // AbilityState 先保留，之後 Stage 2 做 Fire / Spark 時再完整實作。
    enum class KirbyState {
        Normal,
        Inhaling,
        Mouthful,
        AbilityState
    };

    // 用矩形描述場景物件，之後要新增/調整位置會比較直覺。
    struct StageLayout {
        int width;
        QVector<QRectF> platforms;
        QVector<QRectF> blocks;
        QVector<QRectF> holes;
        QRectF portal;
        QRectF goal;
    };

    // ItemType 用來分辨道具種類：番茄補 HP，1UP 補 lives。
    enum class ItemType {
        MaximTomato,
        OneUp
    };

    // Item 記錄一個道具的位置、種類，以及是否已經被 Kirby 吃掉。
    struct Item {
        QRectF rect;
        ItemType type;
        bool collected;
    };

    // EnemyType 用來分辨 Stage 1 敵人：Waddle Dee 會走動，Gordo 是不可消滅的障礙。
    enum class EnemyType {
        WaddleDee,
        Gordo
    };

    // Enemy 記錄敵人的位置、移動範圍、速度與方向。
    // horizontalMove 為 true 代表左右移動，false 代表上下移動。
    struct Enemy {
        QRectF rect;
        EnemyType type;
        float minX;
        float maxX;
        float minY;
        float maxY;
        float speed;
        int direction;
        bool horizontalMove;
        bool active;
    };

    // Projectile 目前只代表吐出的星星彈，會水平飛行並和敵人/Block 碰撞。
    struct Projectile {
        QRectF rect;
        float velocityX;
        bool active;
    };

    // 載入 qrc 中的圖片素材，之後繪圖會重複使用這些 QPixmap。
    void loadAssets();

    // 建立 Stage 1 / Stage 2 的平台、磚塊、洞、Portal 與 Goal 矩形資料。
    void setupStages();

    // 建立 Stage 1 的道具位置，遊戲重新開始時也會呼叫來重設道具。
    void setupItems();

    // 建立 Stage 1 的敵人配置：Frame 1/2/3 都至少有一個敵人。
    void setupEnemies();

    // 把 Kirby、camera 和按鍵狀態重設到目前關卡起點。
    void resetStageStart();

    // 每一幀由 QTimer 呼叫，負責更新 Kirby、camera，最後要求重新繪圖。
    void updateGame();

    // 更新 Kirby 的移動、跳躍、Hover、重力、平台/Block/hole 碰撞。
    void updateKirby();

    // 檢查 Kirby 是否碰到道具；碰到後立刻套用效果並讓道具消失。
    void updateItems();

    // 更新 Stage 1 敵人的移動與碰撞傷害。
    void updateEnemies();

    // 更新吸入範圍，只有 Waddle Dee 可以被吸進嘴裡。
    void updateInhale();

    // 更新星星彈移動，並處理打到 Waddle Dee、Gordo 或 Block 的結果。
    void updateProjectiles();

    // 讓 cameraX 水平跟隨 Kirby，並限制在目前關卡範圍內。
    void updateCamera();

    // 繪製開始畫面與操作提示。
    void drawStartMenu(QPainter &painter);

    // 繪製目前關卡：背景、地板、平台、Block、道具、敵人、Portal/Goal 與 Kirby。
    void drawStage(QPainter &painter);

    // 繪製通關畫面。
    void drawClear(QPainter &painter);

    // 繪製 Game Over 畫面，依目前選項切換 continue / quit 兩張圖片。
    void drawGameOver(QPainter &painter);

    // 在遊戲畫面上顯示目前 lives、HP，以及無敵提示。
    void drawHud(QPainter &painter);

    // 根據目前關卡，把多個 frame 的背景圖排成橫向關卡。
    void drawStageFrames(QPainter &painter);

    // 繪製地板與 hole 缺口。
    void drawGround(QPainter &painter);

    // 繪製平台、Block、道具、敵人、Portal 和 Goal 等場景物件。
    void drawSceneObjects(QPainter &painter);

    // 繪製還沒被吃掉的道具，位置會跟著 cameraX 捲動畫面。
    void drawItems(QPainter &painter);

    // 繪製 Stage 1 的 Waddle Dee 和 Gordo。
    void drawEnemies(QPainter &painter);

    // 繪製 Kirby 吸入時的吸力範圍，方便 Demo 檢查方向與碰撞。
    void drawInhaleArea(QPainter &painter);

    // 繪製吐出的星星彈。
    void drawProjectiles(QPainter &painter);

    // 當星星彈圖片沒有載入時，用 QPainter 畫五角星 placeholder。
    void drawStarPlaceholder(QPainter &painter, const QRectF &target) const;

    // 用同一張圖片重複鋪滿一個世界座標矩形，例如平台。
    void drawTiledPixmap(QPainter &painter, const QPixmap &pixmap, const QRectF &worldRect);

    // 重設整場遊戲，回到 Start Menu，並把 lives / HP 回復初始值。
    void resetGame();

    // Game Over 專用按鍵處理；回傳 true 表示這顆鍵已經被 Continue / Quit 選單使用。
    bool handleGameOverKey(int key);

    // 回傳 Game Over 圖片實際畫在視窗上的位置，滑鼠 hitbox 會依這個矩形換算。
    QRect gameOverImageRect() const;

    // Game Over 的 Continue 按鈕 hitbox。
    QRect gameOverContinueButtonRect() const;

    // Game Over 的 Quit 按鈕 hitbox。
    QRect gameOverQuitButtonRect() const;

    // 依滑鼠位置切換目前 Game Over 選項。
    bool updateGameOverSelectionFromMouse(const QPoint &position);

    // 執行目前 Game Over 選項：Continue 重開，Quit 關閉。
    void activateGameOverSelection();

    // 扣掉一條 life，若 lives 歸零就切換到 Game Over。
    void loseLife();

    // Kirby 受傷時呼叫：扣 HP，必要時扣 life，並給 2 秒無敵。
    void damageKirby();

    // 讓 Kirby 從 Normal 進入 Inhaling 狀態。
    void startInhaling();

    // 放開 X 時停止吸入，從 Inhaling 回到 Normal。
    void stopInhaling();

    // Mouthful 狀態按 Down 時吞下嘴裡敵人；Waddle Dee 不會給能力。
    void swallowMouthfulEnemy();

    // Mouthful 狀態按 X 時吐出星星彈，並回到 Normal。
    void spitStar();

    // 依照道具種類改變 Kirby 狀態，例如補滿 HP 或增加 lives。
    void applyItemEffect(ItemType itemType);

    // 從 Stage 1 進入 Stage 2，並重設 Kirby 到 Stage 2 起點。
    void switchToStage2();

    // 進入 Clear 畫面，停止目前操作狀態。
    void switchToClear();

    // 依照目前 scene 回傳 Stage 1 或 Stage 2 的資料。
    const StageLayout &currentStage() const;

    // 回傳 Kirby 目前的碰撞矩形，碰撞判定都以這個 QRectF 為準。
    QRectF kirbyRect() const;

    // 回傳 Kirby 面前的吸入範圍，方向會依照 Kirby 面向決定。
    QRectF inhaleRect() const;

    // 根據蹲下、飛行、跳躍或跑步狀態，選出要顯示的 Kirby 圖片。
    const QPixmap &currentKirbyPixmap(bool isSquatting) const;

    // 判斷兩個矩形在 x 方向是否有重疊，用於平台與 Block 的落地判定。
    bool horizontalOverlap(const QRectF &a, const QRectF &b) const;

    // 判斷 Kirby 是否碰到 Portal 或 Goal，並稍微放大判定範圍讓操作更容易。
    bool touchesInteractionObject(const QRectF &objectRect) const;

    // 判斷 Kirby 的矩形是否碰到任何 solid block。
    bool collidesWithAnyBlock(const QRectF &rect) const;

    // 判斷 Kirby 腳底中心是否位在 hole 區域內。
    bool isOverHole(const QRectF &rect) const;

    // 判斷 Kirby 腳下是否有地板、平台或 Block 支撐。
    bool hasGroundSupport(const QRectF &rect) const;

    // 紀錄現在顯示哪個畫面。
    Scene m_scene;

    // QTimer 每隔一小段時間觸發一次，形成簡單的遊戲迴圈。
    QTimer m_timer;

    // 第一、二、三階段會用到的圖片素材。
    QPixmap m_startBackground;
    QPixmap m_stageBackdrop;
    QPixmap m_stage1Frame1;
    QPixmap m_stage1Frame2;
    QPixmap m_stage1Frame3;
    QPixmap m_stage2Frame1;
    QPixmap m_stage2Frame2;
    QPixmap m_gameOverContinueImage;
    QPixmap m_gameOverQuitImage;
    QPixmap m_floorTile;
    QPixmap m_blockTile;
    QPixmap m_portalImage;
    QPixmap m_goalImage;
    QPixmap m_lifeIcon;
    QPixmap m_hpFullIcon;
    QPixmap m_hpEmptyIcon;
    QPixmap m_maximTomatoImage;
    QPixmap m_oneUpImage;
    QPixmap m_waddleDeeImage;
    QPixmap m_gordoImage;
    QPixmap m_kirbyStopLeft;
    QPixmap m_kirbyStopRight;
    QPixmap m_kirbyRunLeft;
    QPixmap m_kirbyRunRight;
    QPixmap m_kirbyJump;
    QVector<QPixmap> m_kirbyRunLeftFrames;
    QVector<QPixmap> m_kirbyRunRightFrames;
    QVector<QPixmap> m_kirbyJumpFrames;
    QVector<QPixmap> m_kirbyHoverLeftFrames;
    QVector<QPixmap> m_kirbyHoverRightFrames;
    QPixmap m_kirbySquatLeft;
    QPixmap m_kirbySquatRight;
    QPixmap m_kirbyHoverLeft;
    QPixmap m_kirbyHoverRight;
    QPixmap m_kirbyInhaleLeft;
    QPixmap m_kirbyInhaleRight;
    QPixmap m_kirbyExhaleLeft;
    QPixmap m_kirbyExhaleRight;
    QPixmap m_kirbySpitLeft;
    QPixmap m_kirbySpitRight;
    QPixmap m_kirbyMouthfulLeft;
    QPixmap m_kirbyMouthfulRight;
    QPixmap m_spitStarImage;

    StageLayout m_stage1;
    StageLayout m_stage2;
    QVector<Item> m_items;
    QVector<Enemy> m_enemies;
    QVector<Projectile> m_projectiles;

    // 按鍵狀態：移動、蹲下、飛行都需要知道按鍵是否持續按住。
    bool m_leftPressed;
    bool m_rightPressed;
    bool m_upPressed;
    bool m_downPressed;

    // Kirby 的狀態：面向、是否在地上、是否正在飛行。
    bool m_faceRight;
    bool m_onGround;
    bool m_isHovering;

    // 用來確保 Kirby 必須在空中「再次按 Up」才會進入飛行。
    bool m_canStartHover;

    // KirbyState 控制吸入、Mouthful 與之後保留的 Ability 狀態。
    KirbyState m_kirbyState;
    bool m_hasMouthfulEnemy;
    EnemyType m_mouthfulEnemyType;

    // Kirby 的生命系統。lives 歸零會進入 Game Over，HP 歸零會扣 1 條 life。
    int m_lives;
    int m_hp;

    // 受傷後的短暫無敵時間，用幀數倒數；大約 60 FPS，所以 120 幀約 2 秒。
    int m_invincibleFrames;

    // Game Over 畫面目前選到哪個選項；false 是 Continue，true 是 Quit。
    bool m_gameOverSelectQuit;

    // Hover 中按 X 吐氣後，短暫顯示 exhale sprite；只影響畫面，不改變碰撞或移動邏輯。
    int m_exhaleEffectFrames;

    // Mouthful 按 X 吐星星彈後，短暫顯示 Spit 動作；只影響外觀，不改變 projectile 碰撞。
    int m_spitAnimationFrames;

    // Kirby 一般動作用動畫計時器；跑步、Hover 會依這個數字循環切換圖片。
    int m_animationTimer;

    // Kirby 使用世界座標，cameraX 會決定畫面目前看到關卡的哪一段。
    float m_kirbyX;
    float m_kirbyY;
    float m_kirbyVelocityY;
    float m_cameraX;
};

#endif // MAINWINDOW_H
