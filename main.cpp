#include "raylib.h"
#include "raymath.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

const int COLS = 30;
const int ROWS = 20;
const int SCREEN_W = 1280;
const int SCREEN_H = 800;
const int ORE_PER_TILE = 60;
const float TURRET_RANGE = 5.5f;
const float TURRET_CD = 0.4f;
const int TURRET_DMG = 25;
const int TURRET_COST = 10;
const int MAX_SCORES = 5;
const char* SAVE_FILE = "save.dat";
const char* SCORE_FILE = "scores.dat";

enum class CellType { Empty, Ore, Miner, Belt, Furnace, Turret, Base };
enum class Dir { Right, Down, Left, Up };
enum class Item { None, IronOre, CopperOre, IronPlate, CopperPlate };
enum class OreKind { None, Iron, Copper };
enum class Scene { Menu, Settings, Records, Game, Win, Lose };
enum class EnemyType { Normal, Fast, Tank, Boss };

struct Cell {
    CellType type = CellType::Empty;
    Dir dir = Dir::Right;
    float timer = 0.0f;
    Item item = Item::None;
    OreKind oreKind = OreKind::None;
    int oreAmount = 0;
    int hp = 50;
    int maxHp = 50;
    float animT = 0.0f;
    float flash = 0.0f;
    float aimAngle = 0.0f;
    float beltAnim = 0.0f;
};

struct Enemy {
    float x, z;
    float hp, maxHp, speed;
    EnemyType type = EnemyType::Normal;
    bool alive = true;
    float animT = 0.0f;
    float attackCd = 0.0f;
    int damage = 10;
    int reward = 1;
};

struct Particle {
    float x, y, z;
    float vx, vy, vz;
    float life, maxLife, size;
    Color color;
    bool gravity = true;
};

struct FloatText {
    float x, y, z;
    float life;
    char text[32];
    Color color;
};

struct Score {
    int plates;
    float time;
    int waves;
    int kills;
};

struct GameState {
    Cell grid[COLS][ROWS];
    int ironPlates = 0, copperPlates = 0;
    int oreInGround = 0, minerLevel = 1;
    int baseHP = 100, baseMaxHP = 100;
    float playTime = 0.0f;
    bool paused = false;
    int wave = 0, totalWaves = 5;
    int enemiesToSpawn = 0;
    float spawnTimer = 0.0f;
    float buildTime = 25.0f;
    bool waveActive = false;
    float shake = 0.0f;
    int kills = 0;
    float dayNight = 0.0f;
    bool builtAnyMiner = false;
    bool builtAnyFurnace = false;
    bool gotAnyPlate = false;
};

GameState g;
Scene scene = Scene::Menu;
CellType tool = CellType::Miner;
Dir buildDir = Dir::Right;
int hoverX = -1, hoverZ = -1;
float volume = 0.6f;
Sound sMine, sSmelt, sShoot, sHit, sWave, sBuild, sClick;
std::vector<Enemy> enemies;
std::vector<Particle> particles;
std::vector<FloatText> floats;
Score scores[MAX_SCORES];
int numScores = 0;
float starX[120], starY[120], starSpeed[120];
Camera3D camera;
float camAngle = 0.0f;
float camDist = 22.0f;
char hintMsg[160] = "";
float hintTimer = 0.0f;

const int TOOLBAR_H = 110;
const int TOPBAR_H = 70;

bool InBounds(int x, int z) { return x >= 0 && z >= 0 && x < COLS && z < ROWS; }

Vector2 DirOff(Dir d) {
    switch (d) {
        case Dir::Right: return { 1, 0 };
        case Dir::Down:  return { 0, 1 };
        case Dir::Left:  return { -1, 0 };
        case Dir::Up:    return { 0, -1 };
    }
    return { 0, 0 };
}

float Dist(float x1, float z1, float x2, float z2) {
    float dx = x2 - x1, dz = z2 - z1;
    return sqrtf(dx * dx + dz * dz);
}

Color LerpColor(Color a, Color b, float t) {
    return Color{
        (unsigned char)(a.r + (b.r - a.r) * t),
        (unsigned char)(a.g + (b.g - a.g) * t),
        (unsigned char)(a.b + (b.b - a.b) * t),
        255
    };
}

void SetHint(const char* msg, float time = 3.0f) {
    snprintf(hintMsg, sizeof(hintMsg), "%s", msg);
    hintTimer = time;
}

void AddParticles(float x, float y, float z, int n, Color col, float spread, float life, bool grav = true) {
    for (int i = 0; i < n; i++) {
        Particle p;
        p.x = x; p.y = y; p.z = z;
        float ang = (float)GetRandomValue(0, 360) * PI / 180.0f;
        float sp = (float)GetRandomValue(20, 100) / 100.0f * spread;
        p.vx = cosf(ang) * sp;
        p.vz = sinf(ang) * sp;
        p.vy = (float)GetRandomValue(50, 150) / 100.0f * spread;
        p.life = p.maxLife = life + (float)GetRandomValue(0, 60) / 100.0f;
        p.size = 0.05f + (float)GetRandomValue(0, 30) / 1000.0f;
        p.color = col;
        p.gravity = grav;
        particles.push_back(p);
    }
}

void AddFloat(float x, float y, float z, const char* txt, Color col) {
    FloatText ft;
    ft.x = x; ft.y = y; ft.z = z; ft.life = 1.3f;
    snprintf(ft.text, sizeof(ft.text), "%s", txt);
    ft.color = col;
    floats.push_back(ft);
}

Sound MakeBeep(float freq, float dur, int sr) {
    int n = (int)(sr * dur);
    short* data = (short*)malloc(n * sizeof(short));
    for (int i = 0; i < n; i++) {
        float t = (float)i / sr;
        float env = 1.0f - (float)i / n;
        data[i] = (short)(sinf(2.0f * PI * freq * t) * 12000 * env);
    }
    Wave w = { 0 };
    w.frameCount = n; w.sampleRate = sr; w.sampleSize = 16;
    w.channels = 1; w.data = data;
    Sound s = LoadSoundFromWave(w);
    free(data);
    return s;
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
    AddFloat(COLS / 2.0f, 3.0f, ROWS / 2.0f, "SAVED", GREEN);
    SetHint("Game saved", 2.0f);
}

bool LoadGame() {
    FILE* f = fopen(SAVE_FILE, "rb");
    if (!f) return false;
    fread(&g, sizeof(GameState), 1, f);
    fclose(f);
    return true;
}

void LoadScores() {
    FILE* f = fopen(SCORE_FILE, "rb");
    if (!f) { numScores = 0; return; }
    fread(&numScores, sizeof(int), 1, f);
    if (numScores > MAX_SCORES) numScores = MAX_SCORES;
    fread(scores, sizeof(Score), numScores, f);
    fclose(f);
}

void SaveScores() {
    FILE* f = fopen(SCORE_FILE, "wb");
    if (!f) return;
    fwrite(&numScores, sizeof(int), 1, f);
    fwrite(scores, sizeof(Score), numScores, f);
    fclose(f);
}

void AddScore(int plates, float time, int waves, int kills) {
    Score s{ plates, time, waves, kills };
    int pos = numScores;
    for (int i = 0; i < numScores; i++) {
        if (plates > scores[i].plates || (plates == scores[i].plates && time < scores[i].time)) {
            pos = i;
            break;
        }
    }
    if (pos < MAX_SCORES) {
        if (numScores < MAX_SCORES) numScores++;
        for (int i = numScores - 1; i > pos; i--) scores[i] = scores[i - 1];
        scores[pos] = s;
        SaveScores();
    }
}

void GenerateMap() {
    g = GameState{};
    enemies.clear();
    particles.clear();
    floats.clear();

    auto placeOre = [&](int cx, int cz, int r, OreKind kind) {
        for (int z = cz - r; z <= cz + r; z++)
            for (int x = cx - r; x <= cx + r; x++)
                if (InBounds(x, z) && (x - cx) * (x - cx) + (z - cz) * (z - cz) <= r * r
                    && g.grid[x][z].type == CellType::Empty) {
                    g.grid[x][z].type = CellType::Ore;
                    g.grid[x][z].oreKind = kind;
                    g.grid[x][z].oreAmount = ORE_PER_TILE;
                    g.oreInGround += ORE_PER_TILE;
                }
    };
    placeOre(5, 5, 3, OreKind::Iron);
    placeOre(24, 14, 3, OreKind::Iron);
    placeOre(15, 10, 3, OreKind::Copper);
    placeOre(7, 15, 2, OreKind::Copper);

    g.grid[COLS / 2][ROWS / 2].type = CellType::Base;
    g.grid[COLS / 2][ROWS / 2].hp = g.grid[COLS / 2][ROWS / 2].maxHp = 500;
}

void SpawnWave() {
    g.wave++;
    if (g.wave > g.totalWaves) {
        scene = Scene::Win;
        AddScore(g.ironPlates + g.copperPlates, g.playTime, g.wave - 1, g.kills);
        return;
    }
    bool boss = (g.wave % 3 == 0);
    g.enemiesToSpawn = 4 + g.wave * 4;
    g.spawnTimer = 0;
    g.waveActive = true;
    SetSoundVolume(sWave, volume);
    PlaySound(sWave);
    AddFloat(COLS / 2.0f, 3.0f, ROWS / 2.0f, TextFormat("WAVE %d", g.wave), boss ? RED : YELLOW);
    SetHint(boss ? "BOSS! Defend the base!" : "Wave incoming! Enemies go to base!", 3.0f);
}

void SpawnEnemy(bool boss = false) {
    Enemy e;
    int side = GetRandomValue(0, 3);
    float pad = 2.0f;
    switch (side) {
        case 0: e.x = (float)GetRandomValue(0, COLS); e.z = -pad; break;
        case 1: e.x = (float)GetRandomValue(0, COLS); e.z = ROWS + pad; break;
        case 2: e.x = -pad; e.z = (float)GetRandomValue(0, ROWS); break;
        case 3: e.x = COLS + pad; e.z = (float)GetRandomValue(0, ROWS); break;
    }
    if (boss) {
        e.type = EnemyType::Boss;
        e.maxHp = e.hp = 300.0f + g.wave * 50.0f;
        e.speed = 1.2f; e.damage = 30; e.reward = 10;
    } else {
        int r = GetRandomValue(0, 100);
        if (r < 60) {
            e.type = EnemyType::Normal;
            e.maxHp = e.hp = 30.0f + g.wave * 12.0f;
            e.speed = 1.6f + g.wave * 0.2f;
            e.damage = 10; e.reward = 1;
        } else if (r < 85) {
            e.type = EnemyType::Fast;
            e.maxHp = e.hp = 18.0f + g.wave * 8.0f;
            e.speed = 3.0f + g.wave * 0.3f;
            e.damage = 6; e.reward = 1;
        } else {
            e.type = EnemyType::Tank;
            e.maxHp = e.hp = 70.0f + g.wave * 25.0f;
            e.speed = 1.0f + g.wave * 0.15f;
            e.damage = 20; e.reward = 3;
        }
    }
    e.alive = true;
    enemies.push_back(e);
}

void DamageCell(int x, int z, int dmg) {
    if (!InBounds(x, z)) return;
    Cell& c = g.grid[x][z];
    if (c.type == CellType::Empty || c.type == CellType::Ore) return;
    c.hp -= dmg;
    c.flash = 0.2f;
    if (c.hp <= 0) {
        AddParticles(x + 0.5f, 0.5f, z + 0.5f, 15, ORANGE, 3.0f, 0.6f);
        AddParticles(x + 0.5f, 0.5f, z + 0.5f, 10, Color{80,80,80,255}, 2.0f, 1.0f, false);
        if (c.type == CellType::Base) {
            g.baseHP -= dmg;
            g.shake = 15;
            if (g.baseHP <= 0) {
                scene = Scene::Lose;
                AddScore(g.ironPlates + g.copperPlates, g.playTime, g.wave - 1, g.kills);
            }
        }
        c.type = CellType::Empty;
        c.item = Item::None;
        c.oreKind = OreKind::None;
        c.timer = 0;
    }
}

void UpdateProduction(float dt) {
    for (int z = 0; z < ROWS; z++) for (int x = 0; x < COLS; x++) {
        Cell& c = g.grid[x][z];
        c.animT += dt;
        if (c.flash > 0) c.flash -= dt;
        c.beltAnim += dt * 3.0f;

        if (c.type == CellType::Miner && c.oreAmount > 0 && c.item == Item::None) {
            float interval = 1.5f / (float)g.minerLevel;
            c.timer += dt;
            if (c.timer >= interval) {
                c.timer = 0;
                c.oreAmount--;
                g.oreInGround--;
                c.item = (c.oreKind == OreKind::Iron) ? Item::IronOre : Item::CopperOre;
                AddParticles(x + 0.5f, 0.8f, z + 0.5f, 4, YELLOW, 1.5f, 0.3f);
                SetSoundVolume(sMine, volume * 0.35f);
                PlaySound(sMine);
            }
        }
        else if (c.type == CellType::Furnace) {
            if (c.item == Item::IronOre || c.item == Item::CopperOre) {
                c.timer += dt;
                if (c.timer >= 1.5f) {
                    c.timer = 0;
                    if (c.item == Item::IronOre) { g.ironPlates++; AddFloat(x + 0.5f, 1.5f, z + 0.5f, "+1 Fe", Color{220,220,230,255}); }
                    else { g.copperPlates++; AddFloat(x + 0.5f, 1.5f, z + 0.5f, "+1 Cu", Color{240,160,80,255}); }
                    c.item = Item::None;
                    if (!g.gotAnyPlate) {
                        g.gotAnyPlate = true;
                        SetHint("Great! First plate produced!", 3.0f);
                    }
                    AddParticles(x + 0.5f, 1.2f, z + 0.5f, 6, Color{255,180,80,255}, 1.5f, 0.5f, false);
                    SetSoundVolume(sSmelt, volume * 0.4f);
                    PlaySound(sSmelt);
                }
            }
        }
    }

    for (int z = 0; z < ROWS; z++) for (int x = 0; x < COLS; x++) {
        Cell& c = g.grid[x][z];
        if (c.type != CellType::Belt || c.item == Item::None) continue;
        Vector2 o = DirOff(c.dir);
        int nx = x + (int)o.x, nz = z + (int)o.y;
        if (!InBounds(nx, nz)) continue;
        Cell& n = g.grid[nx][nz];
        if ((n.type == CellType::Belt || n.type == CellType::Furnace) && n.item == Item::None) {
            n.item = c.item; c.item = Item::None;
        }
    }

    for (int z = 0; z < ROWS; z++) for (int x = 0; x < COLS; x++) {
        Cell& c = g.grid[x][z];
        if (c.type != CellType::Miner || c.item == Item::None) continue;
        Vector2 o = DirOff(c.dir);
        int nx = x + (int)o.x, nz = z + (int)o.y;
        if (!InBounds(nx, nz)) continue;
        Cell& n = g.grid[nx][nz];
        if (n.type == CellType::Belt && n.item == Item::None) {
            n.item = c.item; c.item = Item::None;
        }
    }
}

void UpdateTurrets(float dt) {
    for (int z = 0; z < ROWS; z++) for (int x = 0; x < COLS; x++) {
        Cell& c = g.grid[x][z];
        if (c.type != CellType::Turret) continue;
        c.timer += dt;
        float cx = x + 0.5f;
        float cz = z + 0.5f;
        Enemy* target = nullptr;
        float best = TURRET_RANGE;
        for (auto& e : enemies) {
            if (!e.alive) continue;
            float d = Dist(cx, cz, e.x, e.z);
            if (d < best) { best = d; target = &e; }
        }
        if (target) {
            c.aimAngle = atan2f(target->z - cz, target->x - cx);
            if (c.timer >= TURRET_CD) {
                c.timer = 0;
                target->hp -= TURRET_DMG;
                AddParticles(target->x, 0.5f, target->z, 4, YELLOW, 2.0f, 0.3f);
                AddFloat(target->x, 1.5f, target->z, TextFormat("-%d", TURRET_DMG), Color{255,180,80,255});
                SetSoundVolume(sShoot, volume * 0.3f);
                PlaySound(sShoot);
                if (target->hp <= 0) {
                    target->alive = false;
                    g.kills += target->reward;
                    AddParticles(target->x, 0.6f, target->z, 20, RED, 4.0f, 0.8f);
                    AddParticles(target->x, 0.6f, target->z, 10, ORANGE, 3.0f, 0.5f);
                    g.shake = 5;
                    SetSoundVolume(sHit, volume * 0.5f);
                    PlaySound(sHit);
                }
            }
        }
    }
}

void UpdateEnemies(float dt) {
    float bx = COLS / 2.0f + 0.5f;
    float bz = ROWS / 2.0f + 0.5f;
    for (auto& e : enemies) {
        if (!e.alive) continue;
        e.animT += dt;
        float dx = bx - e.x, dz = bz - e.z;
        float d = sqrtf(dx * dx + dz * dz);
        if (d < 0.5f) continue;
        float nx = dx / d, nz = dz / d;
        int ex = (int)floorf(e.x + nx * 0.6f);
        int ez = (int)floorf(e.z + nz * 0.6f);
        bool attacked = false;
        if (InBounds(ex, ez)) {
            Cell& c = g.grid[ex][ez];
            if (c.type != CellType::Empty && c.type != CellType::Ore) {
                e.attackCd -= dt;
                if (e.attackCd <= 0) {
                    e.attackCd = 0.6f;
                    DamageCell(ex, ez, e.damage);
                    AddParticles(e.x, 0.5f, e.z, 4, ORANGE, 2.0f, 0.3f);
                }
                attacked = true;
            }
        }
        if (!attacked) {
            e.x += nx * e.speed * dt;
            e.z += nz * e.speed * dt;
        }
    }
    for (int i = (int)enemies.size() - 1; i >= 0; i--)
        if (!enemies[i].alive) enemies.erase(enemies.begin() + i);
}

void UpdateParticles(float dt) {
    for (int i = (int)particles.size() - 1; i >= 0; i--) {
        Particle& p = particles[i];
        p.x += p.vx * dt; p.y += p.vy * dt; p.z += p.vz * dt;
        if (p.gravity) p.vy -= 8.0f * dt;
        p.life -= dt;
        if (p.life <= 0) particles.erase(particles.begin() + i);
    }
    for (int i = (int)floats.size() - 1; i >= 0; i--) {
        floats[i].y += 1.2f * dt;
        floats[i].life -= dt;
        if (floats[i].life <= 0) floats.erase(floats.begin() + i);
    }
}

void UpdateWaves(float dt) {
    if (g.waveActive) {
        if (g.enemiesToSpawn > 0) {
            g.spawnTimer += dt;
            if (g.spawnTimer >= 0.7f) {
                g.spawnTimer = 0;
                SpawnEnemy(false);
                g.enemiesToSpawn--;
            }
        } else if (enemies.empty()) {
            g.waveActive = false;
            if (g.wave >= g.totalWaves) {
                scene = Scene::Win;
                AddScore(g.ironPlates + g.copperPlates, g.playTime, g.wave, g.kills);
            } else {
                g.buildTime = 25.0f;
                SetHint("Wave cleared! Prepare for next", 3.0f);
            }
        }
    } else if (g.wave < g.totalWaves) {
        g.buildTime -= dt;
        if (g.buildTime <= 0) {
            SpawnWave();
            if (g.wave % 3 == 0) SpawnEnemy(true);
        }
    }
}

void Update(float dt) {
    if (g.paused || scene != Scene::Game) return;
    g.playTime += dt;
    g.dayNight += dt * 0.015f;
    if (g.dayNight > 1.0f) g.dayNight = 0.0f;
    if (hintTimer > 0) hintTimer -= dt;
    UpdateProduction(dt);
    UpdateTurrets(dt);
    UpdateEnemies(dt);
    UpdateParticles(dt);
    UpdateWaves(dt);
    if (g.shake > 0) g.shake -= dt * 40;
    if (g.shake < 0) g.shake = 0;

    if (!g.waveActive && g.wave == 0) {
        if (!g.builtAnyMiner) SetHint("Press 1 and click on brown spot - that is ore", 1.0f);
        else if (!g.gotAnyPlate) SetHint("Place miner > belt (2) > furnace (3). Tight together!", 1.0f);
        else SetHint("Great! Place turrets (4) around base - 10 plates each", 1.0f);
    }
}

Color CellColor(CellType t, OreKind k) {
    switch (t) {
        case CellType::Empty: return Color{ 55, 65, 55, 255 };
        case CellType::Ore: return k == OreKind::Iron ? Color{ 130, 90, 60, 255 } : Color{ 170, 100, 60, 255 };
        case CellType::Miner: return Color{ 210, 180, 60, 255 };
        case CellType::Belt: return Color{ 75, 75, 85, 255 };
        case CellType::Furnace: return Color{ 100, 60, 55, 255 };
        case CellType::Turret: return Color{ 90, 140, 100, 255 };
        case CellType::Base: return Color{ 80, 100, 200, 255 };
    }
    return BLACK;
}

Color ItemColor(Item it) {
    switch (it) {
        case Item::IronOre: return Color{ 180, 180, 190, 255 };
        case Item::CopperOre: return Color{ 220, 130, 60, 255 };
        case Item::IronPlate: return Color{ 230, 230, 240, 255 };
        case Item::CopperPlate: return Color{ 240, 170, 90, 255 };
        default: return BLANK;
    }
}

Color EnemyColor(EnemyType t) {
    switch (t) {
        case EnemyType::Normal: return RED;
        case EnemyType::Fast: return Color{ 240, 200, 40, 255 };
        case EnemyType::Tank: return Color{ 160, 60, 200, 255 };
        case EnemyType::Boss: return Color{ 200, 30, 90, 255 };
    }
    return RED;
}

void DrawGround() {
    for (int z = 0; z < ROWS; z++) for (int x = 0; x < COLS; x++) {
        const Cell& c = g.grid[x][z];
        Color col = CellColor(c.type, c.oreKind);
        if (c.flash > 0) col = Color{ 255, 100, 100, 255 };
        Vector3 pos = { x + 0.5f, 0.0f, z + 0.5f };
        DrawCube(pos, 1.0f, 0.15f, 1.0f, col);
        DrawCubeWires(pos, 1.0f, 0.15f, 1.0f, Color{ 25, 30, 25, 255 });
    }
    if (InBounds(hoverX, hoverZ)) {
        Vector3 pos = { hoverX + 0.5f, 0.13f, hoverZ + 0.5f };
        DrawCubeWires(pos, 1.02f, 0.02f, 1.02f, YELLOW);
        DrawCube(pos, 1.0f, 0.02f, 1.0f, Color{ 255, 255, 100, 60 });

        Cell& hc = g.grid[hoverX][hoverZ];
        Color ghost = Color{ 255, 255, 255, 100 };
        if (tool == CellType::Miner) {
            if (hc.type == CellType::Ore && hc.oreAmount > 0) ghost = Color{ 255, 220, 80, 150 };
            else ghost = Color{ 255, 60, 60, 100 };
            DrawCylinder({ hoverX + 0.5f, 0.35f, hoverZ + 0.5f }, 0.35f, 0.4f, 0.4f, 12, ghost);
        } else if (tool == CellType::Belt) {
            if (hc.type == CellType::Empty) ghost = Color{ 150, 150, 255, 150 };
            else ghost = Color{ 255, 60, 60, 100 };
            DrawCube({ hoverX + 0.5f, 0.15f, hoverZ + 0.5f }, 0.9f, 0.1f, 0.9f, ghost);
        } else if (tool == CellType::Furnace) {
            if (hc.type == CellType::Empty) ghost = Color{ 255, 120, 120, 150 };
            else ghost = Color{ 255, 60, 60, 100 };
            DrawCube({ hoverX + 0.5f, 0.4f, hoverZ + 0.5f }, 0.75f, 0.8f, 0.75f, ghost);
        } else if (tool == CellType::Turret) {
            if (hc.type == CellType::Empty && g.ironPlates >= TURRET_COST) ghost = Color{ 120, 255, 120, 150 };
            else if (hc.type == CellType::Empty) ghost = Color{ 255, 180, 60, 150 };
            else ghost = Color{ 255, 60, 60, 100 };
            DrawCylinder({ hoverX + 0.5f, 0.2f, hoverZ + 0.5f }, 0.4f, 0.4f, 0.3f, 12, ghost);
        } else if (tool == CellType::Empty) {
            if (hc.type == CellType::Ore || hc.type == CellType::Base) ghost = Color{ 255, 60, 60, 100 };
            else ghost = Color{ 255, 100, 100, 150 };
            DrawCubeWires({ hoverX + 0.5f, 0.2f, hoverZ + 0.5f }, 0.9f, 0.3f, 0.9f, RED);
        }
    }
}

void DrawOre(int x, int z, const Cell& c) {
    Color col = (c.oreKind == OreKind::Iron) ? Color{ 90, 60, 40, 255 } : Color{ 140, 70, 30, 255 };
    for (int i = 0; i < 5; i++) {
        float ox = ((i * 13 + x * 7) % 7) / 10.0f - 0.3f;
        float oz = ((i * 11 + z * 5) % 7) / 10.0f - 0.3f;
        float sz = 0.10f + ((i * 3 + x + z) % 5) * 0.02f;
        DrawSphere({ x + 0.5f + ox, 0.10f + sz, z + 0.5f + oz }, sz, col);
    }
}

void DrawMiner(int x, int z, const Cell& c) {
    float bob = sinf(c.animT * 4.0f) * 0.05f;
    DrawCylinder({ x + 0.5f, 0.25f, z + 0.5f }, 0.35f, 0.4f, 0.4f, 12, Color{ 90, 75, 30, 255 });
    DrawCube({ x + 0.5f, 0.55f + bob, z + 0.5f }, 0.6f, 0.4f, 0.6f, Color{ 210, 180, 60, 255 });
    DrawCubeWires({ x + 0.5f, 0.55f + bob, z + 0.5f }, 0.6f, 0.4f, 0.6f, Color{ 140, 110, 20, 255 });
    Vector2 o = DirOff(c.dir);
    Vector3 tip = { x + 0.5f + o.x * 0.5f, 0.85f + bob, z + 0.5f + o.y * 0.5f };
    DrawSphere(tip, 0.1f, Color{ 255, 240, 100, 255 });
    DrawLine3D({ x + 0.5f, 0.85f, z + 0.5f }, tip, YELLOW);
    float blink = 0.5f + 0.5f * sinf(c.animT * 6.0f);
    DrawSphere({ x + 0.5f, 0.85f, z + 0.5f }, 0.08f, Color{ 255, (unsigned char)(200 * blink + 55), 0, 255 });
}

void DrawBelt(int x, int z, const Cell& c) {
    DrawCube({ x + 0.5f, 0.12f, z + 0.5f }, 0.9f, 0.06f, 0.9f, Color{ 60, 60, 70, 255 });
    DrawCubeWires({ x + 0.5f, 0.12f, z + 0.5f }, 0.9f, 0.06f, 0.9f, Color{ 30, 30, 40, 255 });
    Vector2 o = DirOff(c.dir);
    for (int i = 0; i < 3; i++) {
        float off = fmodf(c.beltAnim * 0.6f + i * 0.33f, 1.0f) - 0.5f;
        Vector3 p = { x + 0.5f + o.x * off, 0.18f, z + 0.5f + o.y * off };
        DrawSphere(p, 0.06f, Color{ 200, 200, 200, 255 });
    }
    Vector3 tip = { x + 0.5f + o.x * 0.4f, 0.2f, z + 0.5f + o.y * 0.4f };
    DrawLine3D({ x + 0.5f, 0.2f, z + 0.5f }, tip, YELLOW);
}

void DrawFurnace(int x, int z, const Cell& c) {
    DrawCube({ x + 0.5f, 0.4f, z + 0.5f }, 0.75f, 0.8f, 0.75f, Color{ 70, 45, 45, 255 });
    DrawCubeWires({ x + 0.5f, 0.4f, z + 0.5f }, 0.75f, 0.8f, 0.75f, Color{ 40, 25, 25, 255 });
    DrawCube({ x + 0.5f, 0.82f, z + 0.5f }, 0.5f, 0.05f, 0.5f, Color{ 40, 30, 30, 255 });
    if (c.item == Item::IronOre || c.item == Item::CopperOre) {
        float glow = 0.6f + 0.4f * sinf(c.animT * 8.0f);
        DrawSphere({ x + 0.5f, 0.9f, z + 0.5f }, 0.15f * glow, Color{ 255, 120, 30, 220 });
        DrawSphere({ x + 0.5f, 0.95f, z + 0.5f }, 0.08f, Color{ 255, 240, 150, 255 });
    }
    DrawCylinder({ x + 0.7f, 0.9f, z + 0.3f }, 0.08f, 0.1f, 0.3f, 8, Color{ 50, 50, 55, 255 });
}

void DrawTurret(int x, int z, const Cell& c) {
    DrawCylinder({ x + 0.5f, 0.15f, z + 0.5f }, 0.4f, 0.45f, 0.3f, 12, Color{ 55, 90, 65, 255 });
    DrawCylinder({ x + 0.5f, 0.35f, z + 0.5f }, 0.3f, 0.3f, 0.25f, 12, Color{ 70, 120, 85, 255 });
    float ang = c.aimAngle;
    Vector3 base = { x + 0.5f, 0.55f, z + 0.5f };
    Vector3 tip = { base.x + cosf(ang) * 0.5f, base.y, base.z + sinf(ang) * 0.5f };
    DrawCylinderEx(base, tip, 0.09f, 0.07f, 8, Color{ 60, 60, 65, 255 });
    DrawSphere(tip, 0.09f, Color{ 40, 40, 45, 255 });
    DrawSphere({ x + 0.5f, 0.5f, z + 0.5f }, 0.18f, Color{ 90, 130, 100, 255 });
}

void DrawBase(int x, int z, const Cell& c) {
    float pulse = 0.9f + 0.1f * sinf(c.animT * 3.0f);
    DrawCylinder({ x + 0.5f, 0.1f, z + 0.5f }, 0.7f, 0.7f, 0.2f, 16, Color{ 60, 80, 160, 255 });
    DrawSphere({ x + 0.5f, 0.6f, z + 0.5f }, 0.5f * pulse, Color{ 80, 120, 220, 255 });
    DrawSphere({ x + 0.5f, 0.6f, z + 0.5f }, 0.35f, Color{ 150, 180, 255, 255 });
    DrawSphere({ x + 0.5f, 0.6f, z + 0.5f }, 0.2f, WHITE);
}

void DrawItemIcon(int x, int z, const Cell& c) {
    if (c.item == Item::None) return;
    float bob = sinf(c.animT * 5.0f) * 0.05f;
    DrawSphere({ x + 0.5f, 0.35f + bob, z + 0.5f }, 0.1f, ItemColor(c.item));
}

void DrawBuildings() {
    for (int z = 0; z < ROWS; z++) for (int x = 0; x < COLS; x++) {
        const Cell& c = g.grid[x][z];
        switch (c.type) {
            case CellType::Ore: DrawOre(x, z, c); break;
            case CellType::Miner: DrawMiner(x, z, c); break;
            case CellType::Belt: DrawBelt(x, z, c); break;
            case CellType::Furnace: DrawFurnace(x, z, c); break;
            case CellType::Turret: DrawTurret(x, z, c); break;
            case CellType::Base: DrawBase(x, z, c); break;
            default: break;
        }
        DrawItemIcon(x, z, c);
    }
}

void DrawEnemies() {
    for (auto& e : enemies) {
        float size = 0.4f;
        if (e.type == EnemyType::Fast) size = 0.3f;
        else if (e.type == EnemyType::Tank) size = 0.5f;
        else if (e.type == EnemyType::Boss) size = 0.8f;
        float bob = sinf(e.animT * 6.0f) * 0.08f;
        Color col = EnemyColor(e.type);
        DrawSphere({ e.x, 0.05f, e.z }, size * 0.9f, Color{ 0, 0, 0, 80 });
        DrawSphere({ e.x, 0.5f + bob, e.z }, size, col);
        DrawSphereWires({ e.x, 0.5f + bob, e.z }, size, 8, 8, Color{ 0, 0, 0, 150 });
        Vector3 eye1 = { e.x - size * 0.4f, 0.6f + bob, e.z - size * 0.6f };
        Vector3 eye2 = { e.x + size * 0.4f, 0.6f + bob, e.z - size * 0.6f };
        DrawSphere(eye1, size * 0.15f, WHITE);
        DrawSphere(eye2, size * 0.15f, WHITE);
        float pct = e.hp / e.maxHp;
        float barW = size * 2.2f;
        float barY = 1.2f + size + bob;
        DrawCube({ e.x, barY, e.z }, barW, 0.06f, 0.06f, Color{ 20, 20, 20, 255 });
        Color hc = pct > 0.5f ? GREEN : (pct > 0.25f ? YELLOW : RED);
        DrawCube({ e.x - (1 - pct) * barW / 2, barY, e.z }, barW * pct, 0.07f, 0.07f, hc);
    }
}

void DrawParticles3D() {
    for (auto& p : particles) {
        float alpha = p.life / p.maxLife;
        Color c = p.color;
        c.a = (unsigned char)(255 * alpha);
        DrawSphere({ p.x, p.y, p.z }, p.size, c);
    }
}

void DrawFloats3D() {
    for (auto& f : floats) {
        Vector2 screen = GetWorldToScreen({ f.x, f.y, f.z }, camera);
        Color c = f.color;
        c.a = (unsigned char)(255 * fminf(1.0f, f.life * 1.5f));
        int size = 18;
        int tw = MeasureText(f.text, size);
        DrawText(f.text, (int)screen.x - tw / 2, (int)screen.y, size, c);
    }
}

void DrawToolIcon(CellType t, int cx, int cy, int size, Color col) {
    switch (t) {
        case CellType::Miner:
            DrawCylinder({ (float)cx, (float)cy, 0.0f }, size * 0.35f, size * 0.4f, size * 0.4f, 12, col);
            break;
        case CellType::Belt:
            DrawRectangle(cx - size / 2, cy - size / 4, size, size / 2, col);
            DrawTriangle({ (float)(cx + size / 2), (float)cy },
                         { (float)(cx + size / 2 - size / 3), (float)(cy - size / 3) },
                         { (float)(cx + size / 2 - size / 3), (float)(cy + size / 3) }, YELLOW);
            break;
        case CellType::Furnace:
            DrawRectangle(cx - size / 2, cy - size / 2, size, size, col);
            DrawRectangle(cx - size / 3, cy - size / 3, size / 3, size / 3, YELLOW);
            break;
        case CellType::Turret:
            DrawCircle(cx, cy, size / 2, col);
            DrawRectangle(cx - size / 2, cy - 3, size, 6, DARKGRAY);
            break;
        case CellType::Empty:
            DrawLineEx({ (float)(cx - size / 2), (float)(cy - size / 2) }, { (float)(cx + size / 2), (float)(cy + size / 2) }, 4, col);
            DrawLineEx({ (float)(cx + size / 2), (float)(cy - size / 2) }, { (float)(cx - size / 2), (float)(cy + size / 2) }, 4, col);
            break;
        default: break;
    }
}

const int NUM_TOOLS = 5;
struct ToolInfo { CellType type; const char* key; const char* name; int cost; };
ToolInfo tools[NUM_TOOLS] = {
    { CellType::Miner,   "1", "Miner",   0 },
    { CellType::Belt,    "2", "Belt",    0 },
    { CellType::Furnace, "3", "Furnace", 0 },
    { CellType::Turret,  "4", "Turret",  10 },
    { CellType::Empty,   "5", "Delete",  0 },
};

void DrawToolbar() {
    int y0 = SCREEN_H - TOOLBAR_H;
    DrawRectangle(0, y0, SCREEN_W, TOOLBAR_H, Color{ 18, 20, 26, 245 });
    DrawRectangle(0, y0, SCREEN_W, 2, Color{ 100, 100, 140, 255 });

    Vector2 mouse = GetMousePosition();
    int btnW = 130, btnH = 85;
    int gap = 12;
    int totalW = NUM_TOOLS * btnW + (NUM_TOOLS - 1) * gap;
    int startX = SCREEN_W / 2 - totalW / 2;
    int btnY = y0 + 12;

    for (int i = 0; i < NUM_TOOLS; i++) {
        int x = startX + i * (btnW + gap);
        Rectangle r{ (float)x, (float)btnY, (float)btnW, (float)btnH };
        bool hover = CheckCollisionPointRec(mouse, r);
        bool selected = (tool == tools[i].type);

        Color bg = Color{ 45, 48, 60, 255 };
        if (selected) bg = Color{ 90, 120, 180, 255 };
        else if (hover) bg = Color{ 70, 75, 95, 255 };

        DrawRectangleRec(r, bg);
        DrawRectangleLinesEx(r, selected ? 3 : 2, selected ? YELLOW : (hover ? LIGHTGRAY : Color{ 80, 80, 100, 255 }));

        Color icoCol = Color{ 220, 220, 230, 255 };
        if (tools[i].type == CellType::Miner) icoCol = Color{ 220, 190, 70, 255 };
        if (tools[i].type == CellType::Belt)  icoCol = Color{ 100, 100, 120, 255 };
        if (tools[i].type == CellType::Furnace) icoCol = Color{ 200, 80, 60, 255 };
        if (tools[i].type == CellType::Turret) icoCol = Color{ 90, 160, 110, 255 };
        if (tools[i].type == CellType::Empty) icoCol = Color{ 220, 80, 80, 255 };
        DrawToolIcon(tools[i].type, x + 35, btnY + 35, 30, icoCol);

        int kw = 22;
        DrawRectangle(x + 6, btnY + 6, kw, kw, Color{ 20, 20, 30, 255 });
        DrawRectangleLines(x + 6, btnY + 6, kw, kw, YELLOW);
        DrawText(tools[i].key, x + 12, btnY + 9, 16, YELLOW);

        int nameW = MeasureText(tools[i].name, 18);
        DrawText(tools[i].name, x + btnW / 2 - nameW / 2, btnY + 55, 18, RAYWHITE);

        if (tools[i].cost > 0) {
            Color canAfford = (g.ironPlates >= tools[i].cost) ? GREEN : RED;
            const char* price = TextFormat("%d Fe", tools[i].cost);
            int pw = MeasureText(price, 14);
            DrawText(price, x + btnW / 2 - pw / 2, btnY + btnH - 18, 14, canAfford);
        }

        if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (tools[i].type == CellType::Turret && g.ironPlates < TURRET_COST) {
                SetHint("Not enough plates! Need 10 Fe", 2.0f);
            } else {
                tool = tools[i].type;
                SetSoundVolume(sClick, volume * 0.5f);
                PlaySound(sClick);
            }
        }
    }
}

void DrawTopBar() {
    DrawRectangle(0, 0, SCREEN_W, TOPBAR_H, Color{ 18, 20, 26, 220 });
    DrawRectangle(0, TOPBAR_H - 2, SCREEN_W, 2, Color{ 100, 100, 140, 255 });

    DrawCircle(40, 22, 12, Color{ 220, 220, 230, 255 });
    DrawText(TextFormat("Iron: %d", g.ironPlates), 60, 12, 22, RAYWHITE);

    DrawCircle(220, 22, 12, Color{ 240, 170, 90, 255 });
    DrawText(TextFormat("Copper: %d", g.copperPlates), 240, 12, 22, RAYWHITE);

    DrawText(TextFormat("Kills: %d", g.kills), 400, 12, 22, RED);
    DrawText(TextFormat("Lv.%d", g.minerLevel), 520, 12, 22, YELLOW);

    DrawText("Base:", 620, 12, 20, LIGHTGRAY);
    DrawRectangle(690, 14, 160, 20, Color{ 40, 40, 40, 255 });
    Color hpCol = g.baseHP > g.baseMaxHP / 2 ? GREEN : (g.baseHP > g.baseMaxHP / 4 ? YELLOW : RED);
    DrawRectangle(690, 14, (int)(160.0f * g.baseHP / g.baseMaxHP), 20, hpCol);
    DrawRectangleLines(690, 14, 160, 20, WHITE);
    DrawText(TextFormat("%d", g.baseHP), 860, 12, 20, WHITE);

    if (g.waveActive) {
        DrawText(TextFormat("WAVE %d", g.wave), 970, 12, 26, RED);
        DrawText(TextFormat("Enemies: %d", (int)enemies.size() + g.enemiesToSpawn), 1120, 15, 18, RAYWHITE);
    } else {
        DrawText(TextFormat("Next wave: %.0f s", g.buildTime), 950, 12, 22, YELLOW);
    }
}

void DrawMinimap() {
    int mw = COLS * 4, mh = ROWS * 4;
    int mx = SCREEN_W - mw - 15, my = TOPBAR_H + 15;
    DrawRectangle(mx - 3, my - 3, mw + 6, mh + 6, Color{ 0, 0, 0, 180 });
    for (int z = 0; z < ROWS; z++) for (int x = 0; x < COLS; x++) {
        Color c = CellColor(g.grid[x][z].type, g.grid[x][z].oreKind);
        if (g.grid[x][z].type == CellType::Empty) c = Color{ 40, 50, 40, 200 };
        DrawRectangle(mx + x * 4, my + z * 4, 4, 4, c);
    }
    for (auto& e : enemies)
        DrawRectangle(mx + (int)(e.x * 4) - 1, my + (int)(e.z * 4) - 1, 4, 4, RED);
}

void DrawHint() {
    if (hintTimer <= 0) return;
    int th = 44;
    int ty = TOPBAR_H + 10;
    int tw = MeasureText(hintMsg, 22) + 40;
    int tx = SCREEN_W / 2 - tw / 2;
    DrawRectangle(tx, ty, tw, th, Color{ 30, 40, 70, 230 });
    DrawRectangleLines(tx, ty, tw, th, YELLOW);
    int tW = MeasureText(hintMsg, 22);
    DrawText(hintMsg, SCREEN_W / 2 - tW / 2, ty + 10, 22, RAYWHITE);
}

void UpdateCamera() {
    if (IsKeyDown(KEY_Q)) camAngle -= 1.5f * GetFrameTime();
    if (IsKeyDown(KEY_E)) camAngle += 1.5f * GetFrameTime();
    float wheel = GetMouseWheelMove();
    camDist -= wheel * 1.5f;
    if (camDist < 10.0f) camDist = 10.0f;
    if (camDist > 45.0f) camDist = 45.0f;
    float cx = COLS / 2.0f + sinf(camAngle) * camDist * 0.5f;
    float cz = ROWS / 2.0f + cosf(camAngle) * camDist * 0.5f;
    float cy = camDist * 0.9f;
    if (g.shake > 0) {
        cx += (float)GetRandomValue(-10, 10) / 100.0f * g.shake / 15.0f;
        cz += (float)GetRandomValue(-10, 10) / 100.0f * g.shake / 15.0f;
    }
    camera.position = { cx, cy, cz };
    camera.target = { COLS / 2.0f, 0.0f, ROWS / 2.0f };
    camera.up = { 0, 1, 0 };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;
}

void UpdateMousePick() {
    Ray ray = GetMouseRay(GetMousePosition(), camera);
    if (fabsf(ray.direction.y) > 0.001f) {
        float t = -ray.position.y / ray.direction.y;
        if (t > 0) {
            Vector3 hit = { ray.position.x + ray.direction.x * t, 0, ray.position.z + ray.direction.z * t };
            hoverX = (int)floorf(hit.x);
            hoverZ = (int)floorf(hit.z);
            if (!InBounds(hoverX, hoverZ)) { hoverX = -1; hoverZ = -1; }
            return;
        }
    }
    hoverX = -1;
    hoverZ = -1;
}

bool Button(Rectangle r, const char* text) {
    Vector2 m = GetMousePosition();
    bool hover = CheckCollisionPointRec(m, r);
    Color bg = hover ? Color{ 100, 100, 140, 255 } : Color{ 45, 45, 65, 255 };
    DrawRectangleRec(r, bg);
    DrawRectangleLinesEx(r, 2, hover ? YELLOW : LIGHTGRAY);
    int size = 26;
    int tw = MeasureText(text, size);
    DrawText(text, (int)(r.x + r.width / 2 - tw / 2), (int)(r.y + r.height / 2 - size / 2), size, RAYWHITE);
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

void DrawStars() {
    for (int i = 0; i < 120; i++) {
        starY[i] += starSpeed[i] * GetFrameTime();
        if (starY[i] > SCREEN_H) { starY[i] = 0; starX[i] = (float)GetRandomValue(0, SCREEN_W); }
        float alpha = starSpeed[i] / 60.0f;
        DrawCircle((int)starX[i], (int)starY[i], 1.5f, Color{ 100, 150, 255, (unsigned char)(alpha * 150) });
    }
}

void DrawMenu() {
    ClearBackground({ 12, 12, 22, 255 });
    DrawStars();
    const char* title = "FACTORIO 3D";
    int size = 76;
    int tw = MeasureText(title, size);
    DrawText(title, SCREEN_W / 2 - tw / 2 + 4, 74, size, Color{ 0, 0, 0, 150 });
    DrawText(title, SCREEN_W / 2 - tw / 2, 70, size, ORANGE);
    DrawText("build | defend | survive", SCREEN_W / 2 - 130, 160, 22, GRAY);

    int bw = 320, bh = 55, bx = SCREEN_W / 2 - bw / 2;
    int y = 220;
    if (Button({ (float)bx, (float)y, (float)bw, (float)bh }, "NEW GAME")) {
        GenerateMap(); scene = Scene::Game;
    }
    y += 70;
    if (SaveExists()) {
        if (Button({ (float)bx, (float)y, (float)bw, (float)bh }, "CONTINUE")) {
            if (LoadGame()) scene = Scene::Game;
        }
    } else {
        DrawRectangle(bx, y, bw, bh, { 30, 30, 40, 255 });
        DrawRectangleLines(bx, y, bw, bh, DARKGRAY);
        int t2 = MeasureText("CONTINUE", 24);
        DrawText("CONTINUE", bx + bw / 2 - t2 / 2, y + bh / 2 - 12, 24, DARKGRAY);
    }
    y += 70;
    if (Button({ (float)bx, (float)y, (float)bw, (float)bh }, "RECORDS")) scene = Scene::Records;
    y += 70;
    if (Button({ (float)bx, (float)y, (float)bw, (float)bh }, "SETTINGS")) scene = Scene::Settings;
    y += 70;
    if (Button({ (float)bx, (float)y, (float)bw, (float)bh }, "EXIT")) { CloseWindow(); exit(0); }
}

void DrawSettings() {
    ClearBackground({ 12, 12, 22, 255 });
    DrawStars();
    const char* t = "SETTINGS";
    int tw = MeasureText(t, 44);
    DrawText(t, SCREEN_W / 2 - tw / 2, 50, 44, ORANGE);

    DrawText(TextFormat("Volume: %d%%", (int)(volume * 100)), 320, 170, 30, RAYWHITE);
    if (Button({ 640, 163, 60, 45 }, "+")) volume = fminf(1.0f, volume + 0.1f);
    if (Button({ 720, 163, 60, 45 }, "-")) volume = fmaxf(0.0f, volume - 0.1f);

    DrawText("Controls:", 320, 250, 24, YELLOW);
    DrawText("1-4 - select building, 5 - delete", 320, 280, 20, LIGHTGRAY);
    DrawText("R - rotate, RMB - rotate placed", 320, 305, 20, LIGHTGRAY);
    DrawText("U - upgrade miner (5 plates)", 320, 330, 20, LIGHTGRAY);
    DrawText("Q / E - rotate camera, wheel - zoom", 320, 355, 20, LIGHTGRAY);
    DrawText("F5 - save, F9 - load, ESC - menu", 320, 380, 20, LIGHTGRAY);

    if (Button({ (float)(SCREEN_W / 2 - 100), (float)(SCREEN_H - 100), 200, 50 }, "BACK"))
        scene = Scene::Menu;
}

void DrawRecords() {
    ClearBackground({ 12, 12, 22, 255 });
    DrawStars();
    const char* t = "RECORDS";
    int tw = MeasureText(t, 44);
    DrawText(t, SCREEN_W / 2 - tw / 2, 60, 44, GOLD);
    if (numScores == 0) {
        DrawText("No records yet", SCREEN_W / 2 - 100, 250, 28, GRAY);
    } else {
        DrawText("#   Plates   Time    Waves   Kills", 320, 160, 24, YELLOW);
        for (int i = 0; i < numScores; i++) {
            DrawText(TextFormat("%d      %d      %.1f       %d      %d",
                i + 1, scores[i].plates, scores[i].time, scores[i].waves, scores[i].kills),
                320, 210 + i * 40, 24, RAYWHITE);
        }
    }
    if (Button({ (float)(SCREEN_W / 2 - 100), (float)(SCREEN_H - 100), 200, 50 }, "BACK"))
        scene = Scene::Menu;
}

void DrawEndScreen(bool win) {
    ClearBackground(win ? Color{ 20, 50, 25, 255 } : Color{ 50, 15, 15, 255 });
    DrawStars();
    const char* t = win ? "VICTORY!" : "DEFEAT";
    int size = 80;
    int tw = MeasureText(t, size);
    DrawText(t, SCREEN_W / 2 - tw / 2 + 4, 104, size, Color{ 0, 0, 0, 150 });
    DrawText(t, SCREEN_W / 2 - tw / 2, 100, size, win ? GOLD : RED);

    DrawText(win ? "You survived all waves!" : "Your base was destroyed...",
             SCREEN_W / 2 - 180, 210, 26, RAYWHITE);

    DrawText(TextFormat("Time: %.1f sec", g.playTime), 400, 280, 28, RAYWHITE);
    DrawText(TextFormat("Iron plates: %d", g.ironPlates), 400, 320, 28, RAYWHITE);
    DrawText(TextFormat("Copper plates: %d", g.copperPlates), 400, 360, 28, RAYWHITE);
    DrawText(TextFormat("Kills: %d", g.kills), 400, 400, 28, RED);

    if (Button({ (float)(SCREEN_W / 2 - 150), 470, 300, 55 }, "PLAY AGAIN")) {
        GenerateMap(); scene = Scene::Game;
    }
    if (Button({ (float)(SCREEN_W / 2 - 150), 540, 300, 55 }, "MAIN MENU")) scene = Scene::Menu;
}

int main() {
    InitWindow(SCREEN_W, SCREEN_H, "Factorio 3D");
    SetExitKey(KEY_NULL);
    InitAudioDevice();
    SetTargetFPS(60);

    sMine  = MakeBeep(660.0f, 0.06f, 22050);
    sSmelt = MakeBeep(440.0f, 0.14f, 22050);
    sShoot = MakeBeep(900.0f, 0.04f, 22050);
    sHit   = MakeBeep(160.0f, 0.20f, 22050);
    sWave  = MakeBeep(300.0f, 0.30f, 22050);
    sBuild = MakeBeep(550.0f, 0.08f, 22050);
    sClick = MakeBeep(700.0f, 0.05f, 22050);

    for (int i = 0; i < 120; i++) {
        starX[i] = (float)GetRandomValue(0, SCREEN_W);
        starY[i] = (float)GetRandomValue(0, SCREEN_H);
        starSpeed[i] = (float)GetRandomValue(10, 60);
    }

    LoadScores();
    GenerateMap();

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        if (scene == Scene::Game) {
            UpdateCamera();
            UpdateMousePick();

            if (IsKeyPressed(KEY_ONE))   { tool = CellType::Miner;   SetSoundVolume(sClick, volume * 0.5f); PlaySound(sClick); }
            if (IsKeyPressed(KEY_TWO))   { tool = CellType::Belt;    SetSoundVolume(sClick, volume * 0.5f); PlaySound(sClick); }
            if (IsKeyPressed(KEY_THREE)) { tool = CellType::Furnace; SetSoundVolume(sClick, volume * 0.5f); PlaySound(sClick); }
            if (IsKeyPressed(KEY_FOUR))  {
                if (g.ironPlates >= TURRET_COST) { tool = CellType::Turret; SetSoundVolume(sClick, volume * 0.5f); PlaySound(sClick); }
                else SetHint("Turret needs 10 iron plates!", 2.0f);
            }
            if (IsKeyPressed(KEY_FIVE))  { tool = CellType::Empty;   SetSoundVolume(sClick, volume * 0.5f); PlaySound(sClick); }
            if (IsKeyPressed(KEY_R)) buildDir = (Dir)(((int)buildDir + 1) % 4);
            if (IsKeyPressed(KEY_P)) g.paused = !g.paused;
            if (IsKeyPressed(KEY_ESCAPE)) scene = Scene::Menu;
            if (IsKeyPressed(KEY_F5)) SaveGame();
            if (IsKeyPressed(KEY_F9)) { if (LoadGame()) SetHint("Game loaded", 2.0f); }
            if (IsKeyPressed(KEY_U)) {
                if (g.ironPlates >= 5) {
                    g.ironPlates -= 5;
                    g.minerLevel++;
                    AddFloat(COLS / 2.0f, 3.0f, ROWS / 2.0f, "MINER UPGRADED!", YELLOW);
                    SetHint(TextFormat("Upgrade! Miner level: %d", g.minerLevel), 2.0f);
                    SetSoundVolume(sBuild, volume);
                    PlaySound(sBuild);
                } else {
                    SetHint("Upgrade needs 5 plates", 2.0f);
                }
            }

            if (hoverX >= 0 && hoverZ >= 0) {
                Cell& c = g.grid[hoverX][hoverZ];
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    bool placed = false;
                    const char* failReason = nullptr;
                    if (tool == CellType::Empty) {
                        if (c.type != CellType::Ore && c.type != CellType::Base) {
                            c.type = CellType::Empty; c.item = Item::None;
                            c.timer = 0; c.oreAmount = 0; c.oreKind = OreKind::None;
                            placed = true;
                        } else failReason = "Cannot delete this";
                    }
                    else if (tool == CellType::Miner) {
                        if (c.type == CellType::Ore && c.oreAmount > 0) {
                            c.type = CellType::Miner; c.dir = buildDir;
                            c.hp = c.maxHp = 50; placed = true;
                            if (!g.builtAnyMiner) { g.builtAnyMiner = true; SetHint("Miner ready! Place belt (2) in arrow direction", 4.0f); }
                        } else failReason = "Miner only on ore (brown)";
                    }
                    else if (tool == CellType::Belt) {
                        if (c.type == CellType::Empty) {
                            c.type = CellType::Belt; c.dir = buildDir;
                            c.hp = c.maxHp = 20; placed = true;
                        } else failReason = "Belt only on empty cell";
                    }
                    else if (tool == CellType::Furnace) {
                        if (c.type == CellType::Empty) {
                            c.type = CellType::Furnace;
                            c.hp = c.maxHp = 50; placed = true;
                            if (!g.builtAnyFurnace) { g.builtAnyFurnace = true; SetHint("Furnace ready! Watch Iron counter", 4.0f); }
                        } else failReason = "Furnace only on empty cell";
                    }
                    else if (tool == CellType::Turret) {
                        if (c.type == CellType::Empty && g.ironPlates >= TURRET_COST) {
                            g.ironPlates -= TURRET_COST;
                            c.type = CellType::Turret;
                            c.hp = c.maxHp = 100; placed = true;
                            AddFloat(hoverX + 0.5f, 1.5f, hoverZ + 0.5f, "-10 Fe", RED);
                        } else if (c.type != CellType::Empty) failReason = "Cell is occupied";
                        else failReason = "Need 10 iron plates";
                    }
                    if (placed) {
                        SetSoundVolume(sBuild, volume * 0.4f);
                        PlaySound(sBuild);
                        AddParticles(hoverX + 0.5f, 0.3f, hoverZ + 0.5f, 6, WHITE, 1.5f, 0.25f, false);
                    } else if (failReason) {
                        SetHint(failReason, 2.0f);
                    }
                }
                if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
                    if (c.type == CellType::Belt || c.type == CellType::Miner)
                        c.dir = (Dir)(((int)c.dir + 1) % 4);
                }
            }

            Update(dt);
        }

        BeginDrawing();
        ClearBackground(Color{ 10, 12, 18, 255 });

        if (scene == Scene::Menu) DrawMenu();
        else if (scene == Scene::Settings) DrawSettings();
        else if (scene == Scene::Records) DrawRecords();
        else if (scene == Scene::Win) DrawEndScreen(true);
        else if (scene == Scene::Lose) DrawEndScreen(false);
        else if (scene == Scene::Game) {
            BeginMode3D(camera);
            DrawSphere({ camera.position.x, camera.position.y, camera.position.z }, 300.0f, Color{ 30, 40, 70, 255 });
            DrawGround();
            DrawBuildings();
            DrawEnemies();
            DrawParticles3D();
            EndMode3D();

            float nightFactor = 0.0f;
            float dn = g.dayNight;
            if (dn < 0.25f) nightFactor = (0.25f - dn) / 0.25f * 0.6f;
            if (dn > 0.75f) nightFactor = (dn - 0.75f) / 0.25f * 0.6f;
            if (nightFactor > 0)
                DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{ 10, 10, 40, (unsigned char)(nightFactor * 255) });

            DrawFloats3D();
            DrawTopBar();
            DrawMinimap();
            DrawHint();
            DrawToolbar();

            if (g.paused) {
                DrawRectangle(0, 0, SCREEN_W, SCREEN_H, { 0, 0, 0, 150 });
                DrawText("PAUSE", SCREEN_W / 2 - 70, SCREEN_H / 2 - 30, 44, YELLOW);
                DrawText("Press P to continue", SCREEN_W / 2 - 100, SCREEN_H / 2 + 35, 20, LIGHTGRAY);
            }
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}