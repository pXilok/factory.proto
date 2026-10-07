#include "raylib.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

const int TILE = 32;
const int COLS = 30;
const int ROWS = 20;
const int SCREEN_W = COLS * TILE;
const int SCREEN_H = ROWS * TILE + 80;
const int WIN_TARGET = 30;
const int ORE_PER_TILE = 50;
const char* SAVE_FILE = "save.dat";

enum class CellType { Empty, Ore, Miner, Belt, Furnace };
enum class Dir { Right, Down, Left, Up };
enum class Item { None, IronOre, CopperOre, IronPlate, CopperPlate };
enum class OreKind { None, Iron, Copper };

struct Cell {
    CellType type = CellType::Empty;
    Dir dir = Dir::Right;
    float timer = 0.0f;
    Item item = Item::None;
    OreKind oreKind = OreKind::None;
    int oreAmount = 0;
};

struct GameState {
    Cell grid[COLS][ROWS];
    int ironPlates = 0;
    int copperPlates = 0;
    int oreInGround = 0;
    int minerLevel = 1;
    float playTime = 0.0f;
    bool paused = false;
    bool won = false;
};

enum class Scene { Menu, Settings, Game, Win };

Sound beepMine, beepSmelt;
float volume = 0.5f;
Scene scene = Scene::Menu;
GameState g;
CellType tool = CellType::Belt;
Dir buildDir = Dir::Right;
int hoverX = 0, hoverY = 0;

bool InBounds(int x, int y) { return x >= 0 && y >= 0 && x < COLS && y < ROWS; }

Vector2 DirOff(Dir d) {
    switch (d) {
        case Dir::Right: return { 1, 0 };
        case Dir::Down:  return { 0, 1 };
        case Dir::Left:  return { -1, 0 };
        case Dir::Up:    return { 0, -1 };
    }
    return { 0, 0 };
}

bool SaveExists() {
    FILE* f = fopen(SAVE_FILE, "rb");
    if (f) { fclose(f); return true; }
    return false;
}

void SaveGame() {
    FILE* f = fopen(SAVE_FILE, "wb");
    if (!f) return;
    fwrite(&g, sizeof(GameState), 1, f);
    fclose(f);
}

bool LoadGame() {
    FILE* f = fopen(SAVE_FILE, "rb");
    if (!f) return false;
    fread(&g, sizeof(GameState), 1, f);
    fclose(f);
    return true;
}

void GenerateMap() {
    g = GameState{};
    auto placeOre = [&](int cx, int cy, int r, OreKind kind) {
        for (int y = cy - r; y <= cy + r; y++)
            for (int x = cx - r; x <= cx + r; x++)
                if (InBounds(x, y) && (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) {
                    g.grid[x][y].type = CellType::Ore;
                    g.grid[x][y].oreKind = kind;
                    g.grid[x][y].oreAmount = ORE_PER_TILE;
                    g.oreInGround += ORE_PER_TILE;
                }
    };
    placeOre(5, 5, 3, OreKind::Iron);
    placeOre(24, 14, 3, OreKind::Iron);
    placeOre(15, 10, 3, OreKind::Copper);
    placeOre(7, 15, 2, OreKind::Copper);
}

Sound MakeBeep(float freq, float duration, int sampleRate) {
    int n = (int)(sampleRate * duration);
    short* data = (short*)malloc(n * sizeof(short));
    for (int i = 0; i < n; i++) {
        float t = (float)i / sampleRate;
        float env = 1.0f - (float)i / n;
        data[i] = (short)(sinf(2.0f * PI * freq * t) * 12000 * env);
    }
    Wave w = { 0 };
    w.frameCount = n;
    w.sampleRate = sampleRate;
    w.sampleSize = 16;
    w.channels = 1;
    w.data = data;
    Sound s = LoadSoundFromWave(w);
    free(data);
    return s;
}

void Update(float dt) {
    if (g.paused || g.won) return;
    g.playTime += dt;

    for (int y = 0; y < ROWS; y++) for (int x = 0; x < COLS; x++) {
        Cell& c = g.grid[x][y];
        if (c.type != CellType::Miner) continue;
        if (c.oreAmount <= 0) continue;
        if (c.item != Item::None) continue;
        float interval = 1.5f / (float)g.minerLevel;
        c.timer += dt;
        if (c.timer >= interval) {
            c.timer = 0;
            c.oreAmount--;
            g.oreInGround--;
            c.item = (c.oreKind == OreKind::Iron) ? Item::IronOre : Item::CopperOre;
            SetSoundVolume(beepMine, volume);
            PlaySound(beepMine);
        }
    }

    for (int y = 0; y < ROWS; y++) for (int x = 0; x < COLS; x++) {
        Cell& c = g.grid[x][y];
        if (c.type != CellType::Belt || c.item == Item::None) continue;
        Vector2 off = DirOff(c.dir);
        int nx = x + (int)off.x, ny = y + (int)off.y;
        if (!InBounds(nx, ny)) continue;
        Cell& n = g.grid[nx][ny];
        if ((n.type == CellType::Belt || n.type == CellType::Furnace) && n.item == Item::None) {
            n.item = c.item;
            c.item = Item::None;
        }
    }

    for (int y = 0; y < ROWS; y++) for (int x = 0; x < COLS; x++) {
        Cell& c = g.grid[x][y];
        if (c.type != CellType::Miner || c.item == Item::None) continue;
        Vector2 off = DirOff(c.dir);
        int nx = x + (int)off.x, ny = y + (int)off.y;
        if (!InBounds(nx, ny)) continue;
        Cell& n = g.grid[nx][ny];
        if (n.type == CellType::Belt && n.item == Item::None) {
            n.item = c.item;
            c.item = Item::None;
        }
    }

    for (int y = 0; y < ROWS; y++) for (int x = 0; x < COLS; x++) {
        Cell& c = g.grid[x][y];
        if (c.type != CellType::Furnace) continue;
        if (c.item == Item::IronOre || c.item == Item::CopperOre) {
            c.timer += dt;
            if (c.timer >= 1.5f) {
                c.timer = 0;
                if (c.item == Item::IronOre) g.ironPlates++;
                else g.copperPlates++;
                c.item = Item::None;
                SetSoundVolume(beepSmelt, volume);
                PlaySound(beepSmelt);
            }
        }
    }

    if (g.ironPlates + g.copperPlates >= WIN_TARGET && !g.won) {
        g.won = true;
        scene = Scene::Win;
    }
}

Color CellColor(const Cell& c) {
    switch (c.type) {
        case CellType::Empty: return { 40, 40, 45, 255 };
        case CellType::Ore:
            return c.oreKind == OreKind::Iron ? Color{ 130, 90, 60, 255 } : Color{ 180, 100, 60, 255 };
        case CellType::Miner: return { 220, 190, 60, 255 };
        case CellType::Belt:  return { 80, 80, 90, 255 };
        case CellType::Furnace: return { 200, 80, 60, 255 };
    }
    return BLACK;
}

Color ItemColor(Item it) {
    switch (it) {
        case Item::IronOre:     return { 170, 170, 180, 255 };
        case Item::CopperOre:   return { 220, 130, 60, 255 };
        case Item::IronPlate:   return { 220, 220, 230, 255 };
        case Item::CopperPlate: return { 240, 160, 80, 255 };
        default: return BLANK;
    }
}

void DrawGame() {
    for (int y = 0; y < ROWS; y++) for (int x = 0; x < COLS; x++) {
        const Cell& c = g.grid[x][y];
        Rectangle r{ (float)(x * TILE), (float)(y * TILE), (float)TILE, (float)TILE };
        DrawRectangleRec(r, CellColor(c));
        DrawRectangleLinesEx(r, 1, { 20, 20, 25, 255 });
        if (c.type == CellType::Belt || c.type == CellType::Miner) {
            Vector2 off = DirOff(c.dir);
            Vector2 cen{ x * TILE + TILE / 2.0f, y * TILE + TILE / 2.0f };
            Vector2 tip{ cen.x + off.x * 10, cen.y + off.y * 10 };
            DrawLineEx(cen, tip, 2, LIGHTGRAY);
        }
        if (c.item != Item::None)
            DrawCircle(x * TILE + TILE / 2, y * TILE + TILE / 2, 6, ItemColor(c.item));
    }
    if (InBounds(hoverX, hoverY) && hoverY < ROWS)
        DrawRectangleLinesEx({ (float)(hoverX * TILE), (float)(hoverY * TILE), (float)TILE, (float)TILE }, 2, YELLOW);

    int y0 = ROWS * TILE;
    DrawRectangle(0, y0, SCREEN_W, 80, { 25, 25, 30, 255 });
    DrawText(TextFormat("Iron: %d  Copper: %d  /%d total", g.ironPlates, g.copperPlates, WIN_TARGET), 10, y0 + 8, 20, RAYWHITE);
    DrawText(TextFormat("Time: %.1f  Ore left: %d  Miner Lv.%d", g.playTime, g.oreInGround, g.minerLevel), 10, y0 + 30, 16, LIGHTGRAY);
    DrawText("1-Miner 2-Belt 3-Furnace 4-Del R-Rot U-Upgrade P-Pause F5-Save F9-Load ESC-Menu", 10, y0 + 52, 14, GRAY);
}

bool Button(Rectangle r, const char* text) {
    Vector2 m = GetMousePosition();
    bool hover = CheckCollisionPointRec(m, r);
    Color bg = hover ? Color{ 90, 90, 120, 255 } : Color{ 50, 50, 70, 255 };
    DrawRectangleRec(r, bg);
    DrawRectangleLinesEx(r, 2, hover ? YELLOW : LIGHTGRAY);
    int size = 26;
    int tw = MeasureText(text, size);
    DrawText(text, (int)(r.x + r.width / 2 - tw / 2), (int)(r.y + r.height / 2 - size / 2), size, RAYWHITE);
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

void DrawMenu() {
    ClearBackground({ 15, 15, 25, 255 });
    const char* title = "FACTORIO PROTO";
    int tw = MeasureText(title, 60);
    DrawText(title, SCREEN_W / 2 - tw / 2, 80, 60, ORANGE);

    int bw = 300, bh = 60, bx = SCREEN_W / 2 - bw / 2;
    if (Button({ (float)bx, 220, (float)bw, (float)bh }, "NEW GAME")) {
        GenerateMap();
        scene = Scene::Game;
    }
    if (SaveExists()) {
        if (Button({ (float)bx, 300, (float)bw, (float)bh }, "CONTINUE")) {
            LoadGame();
            scene = Scene::Game;
        }
    } else {
        DrawRectangle(bx, 300, bw, bh, { 30, 30, 40, 255 });
        DrawRectangleLines(bx, 300, bw, bh, DARKGRAY);
        int t2 = MeasureText("CONTINUE", 26);
        DrawText("CONTINUE", bx + bw / 2 - t2 / 2, 300 + bh / 2 - 13, 26, DARKGRAY);
    }
    if (Button({ (float)bx, 380, (float)bw, (float)bh }, "SETTINGS")) scene = Scene::Settings;
    if (Button({ (float)bx, 460, (float)bw, (float)bh }, "EXIT")) {
        CloseWindow();
        exit(0);
    }
}

void DrawSettings() {
    ClearBackground({ 15, 15, 25, 255 });
    DrawText("SETTINGS", SCREEN_W / 2 - 100, 80, 40, ORANGE);

    DrawText(TextFormat("Volume: %d%%", (int)(volume * 100)), 200, 200, 30, RAYWHITE);
    if (Button({ 500, 195, 60, 45 }, "+")) volume = fminf(1.0f, volume + 0.1f);
    if (Button({ 580, 195, 60, 45 }, "-")) volume = fmaxf(0.0f, volume - 0.1f);

    DrawText("Controls:", 200, 280, 24, YELLOW);
    DrawText("1 - Miner  2 - Belt  3 - Furnace  4 - Delete", 200, 310, 20, LIGHTGRAY);
    DrawText("R - Rotate  U - Upgrade Miner  P - Pause", 200, 335, 20, LIGHTGRAY);
    DrawText("F5 - Save  F9 - Load  ESC - Menu", 200, 360, 20, LIGHTGRAY);

    if (Button({ (float)(SCREEN_W / 2 - 100), (float)(SCREEN_H - 100), 200, 50 }, "BACK"))
        scene = Scene::Menu;
}

void DrawWin() {
    ClearBackground({ 20, 40, 20, 255 });
    const char* t = "WIN!";
    int tw = MeasureText(t, 70);
    DrawText(t, SCREEN_W / 2 - tw / 2, 100, 70, GOLD);

    DrawText(TextFormat("Time: %.1f seconds", g.playTime), 300, 220, 28, RAYWHITE);
    DrawText(TextFormat("Iron plates: %d", g.ironPlates), 300, 260, 28, RAYWHITE);
    DrawText(TextFormat("Copper plates: %d", g.copperPlates), 300, 300, 28, RAYWHITE);

    if (Button({ (float)(SCREEN_W / 2 - 150), 400, 300, 60 }, "PLAY AGAIN")) {
        GenerateMap();
        scene = Scene::Game;
    }
    if (Button({ (float)(SCREEN_W / 2 - 150), 480, 300, 60 }, "MAIN MENU")) {
        scene = Scene::Menu;
    }
}

int main() {
    InitWindow(SCREEN_W, SCREEN_H, "Factorio Proto");
    InitAudioDevice();
    SetTargetFPS(60);

    beepMine = MakeBeep(660.0f, 0.08f, 22050);
    beepSmelt = MakeBeep(440.0f, 0.15f, 22050);

    GenerateMap();

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        Vector2 m = GetMousePosition();
        hoverX = (int)(m.x / TILE);
        hoverY = (int)(m.y / TILE);

        BeginDrawing();

        if (scene == Scene::Menu) {
            DrawMenu();
        }
        else if (scene == Scene::Settings) {
            DrawSettings();
        }
        else if (scene == Scene::Win) {
            DrawWin();
        }
        else if (scene == Scene::Game) {
            if (IsKeyPressed(KEY_ONE))   tool = CellType::Miner;
            if (IsKeyPressed(KEY_TWO))   tool = CellType::Belt;
            if (IsKeyPressed(KEY_THREE)) tool = CellType::Furnace;
            if (IsKeyPressed(KEY_FOUR))  tool = CellType::Empty;
            if (IsKeyPressed(KEY_R)) buildDir = (Dir)(((int)buildDir + 1) % 4);
            if (IsKeyPressed(KEY_P)) g.paused = !g.paused;
            if (IsKeyPressed(KEY_ESCAPE)) scene = Scene::Menu;
            if (IsKeyPressed(KEY_F5)) SaveGame();
            if (IsKeyPressed(KEY_F9)) LoadGame();
            if (IsKeyPressed(KEY_U) && g.ironPlates + g.copperPlates >= 5) {
                int need = 5;
                if (g.ironPlates >= need) g.ironPlates -= need;
                else { need -= g.ironPlates; g.ironPlates = 0; g.copperPlates -= need; }
                g.minerLevel++;
            }

            if (InBounds(hoverX, hoverY) && hoverY < ROWS) {
                Cell& c = g.grid[hoverX][hoverY];
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    if (tool == CellType::Empty) {
                        c.type = CellType::Empty;
                        c.item = Item::None;
                        c.timer = 0;
                        c.oreAmount = 0;
                        c.oreKind = OreKind::None;
                    }
                    else if (tool == CellType::Miner) {
                        if (c.type == CellType::Ore && c.oreAmount > 0) {
                            c.type = CellType::Miner;
                            c.dir = buildDir;
                        }
                    }
                    else if (tool == CellType::Belt) {
                        if (c.type == CellType::Empty) {
                            c.type = CellType::Belt;
                            c.dir = buildDir;
                        }
                    }
                    else if (tool == CellType::Furnace) {
                        if (c.type == CellType::Empty) c.type = CellType::Furnace;
                    }
                }
                if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
                    if (c.type == CellType::Belt || c.type == CellType::Miner)
                        c.dir = (Dir)(((int)c.dir + 1) % 4);
                }
            }

            Update(dt);

            ClearBackground({ 15, 15, 20, 255 });
            DrawGame();
            if (g.paused) DrawText("PAUSE", SCREEN_W / 2 - 60, SCREEN_H / 2 - 20, 40, YELLOW);
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}