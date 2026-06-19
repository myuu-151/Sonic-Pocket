#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr int kLogicalWidth = 160;
constexpr int kLogicalHeight = 152;
constexpr int kDefaultWindowScale = 3;
constexpr int kStageWidth = 6400;
constexpr int kStageHeight = 992;
constexpr int kCollisionTileSize = 8;
constexpr int kCollisionTilesWide = kStageWidth / kCollisionTileSize;
constexpr int kCollisionTilesHigh = kStageHeight / kCollisionTileSize;
constexpr int kLevelBlocksWide = kStageWidth / 32;
constexpr int kLevelBlocksHigh = kStageHeight / 32;
constexpr int kCollisionDefSize = 130;
constexpr float kCameraMinX = 64.0F;
constexpr float kCameraMinY = 64.0F;
constexpr float kCameraMaxX = static_cast<float>(kStageWidth - 224);
constexpr float kCameraMaxY = static_cast<float>(kStageHeight - 216);
constexpr float kPlayerStartX = 112.0F;
constexpr float kPlayerHalfWidth = 6.0F;
constexpr float kPlayerHalfHeight = 13.0F;
constexpr float kAnimationAirThreshold = 0.06F;
constexpr Sint16 kGamepadDeadzone = 8000;
constexpr int kFixedOne = 0x100;
constexpr int kGroundAcceleration = 0x20;
constexpr int kGroundFriction = 0x20;
constexpr int kGroundMaxSpeed = 0x800;
constexpr int kGroundSkidDeceleration = 0x120;
constexpr int kSlopeGravity = 0x18;
constexpr int kSlopeProbeRadius = 4;
constexpr int kGroundProbeUp = 18;
constexpr int kGroundProbeDown = 24;
constexpr int kSkidAnimationTicks = 12;
constexpr int kSkidDustTicks = 8;
constexpr int kSkidDustSpawnInterval = 6;
constexpr float kSkidDustFootOffsetY = 11.0F;
constexpr int kPeeloutSpeed = kGroundMaxSpeed;
constexpr int kAirAcceleration = 0x10;
constexpr int kAirMaxXSpeed = 0x800;
constexpr int kGravity = 0x80;
constexpr int kFallMaxSpeed = 0xF00;
constexpr int kJumpImpulse = 0x900;
constexpr int kJumpReleaseLimit = 0x400;
constexpr float kCameraFollowRight = 48.0F;
constexpr float kCameraFollowLeft = 112.0F;
constexpr float kCameraFollowY = 76.0F;
constexpr float kCameraFollowStep = 2.0F;

struct Texture {
    SDL_Texture* value = nullptr;

    Texture() = default;
    explicit Texture(SDL_Texture* texture) : value(texture) {}
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& other) noexcept : value(other.value) { other.value = nullptr; }
    Texture& operator=(Texture&& other) noexcept {
        if (this != &other) {
            SDL_DestroyTexture(value);
            value = other.value;
            other.value = nullptr;
        }
        return *this;
    }
    ~Texture() { SDL_DestroyTexture(value); }
};

struct SpriteFrame {
    Texture texture;
    float width = 0.0F;
    float height = 0.0F;
    float origin_x = 0.0F;
    float origin_y = 0.0F;
};

struct AnimationFrame {
    SpriteFrame sprite;
    float duration_frames = 1.0F;
};

struct AnimationSequence {
    std::vector<AnimationFrame> frames;
};

struct SonicAnimations {
    AnimationSequence idle;
    AnimationSequence walk;
    AnimationSequence run;
    AnimationSequence peelout;
    AnimationSequence skid;
    AnimationSequence jump;
    AnimationSequence fall;
    AnimationSequence push;
    AnimationSequence look_up;
    AnimationSequence look_down;
    AnimationSequence balance;
};

enum class AnimationState {
    Idle,
    Walk,
    Run,
    Peelout,
    Skid,
    Jump,
    Fall,
    Push,
    LookUp,
    LookDown,
    Balance,
};

struct Application {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Gamepad* gamepad = nullptr;

    ~Application() {
        SDL_CloseGamepad(gamepad);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
    }
};

struct DustPuff {
    float x = 0.0F;
    float y = 0.0F;
    int ticks = 0;
};

struct CollisionResponse {
    int response = 0;
    int angle = 0;
};

struct RomCollisionHit {
    bool hit = false;
    int delta_y = 0;
    int angle = 0;
};

struct Player {
    int x_raw = static_cast<int>(kPlayerStartX * kFixedOne);
    int y_raw = 0;
    int ground_speed = 0;
    int velocity_x = 0;
    int velocity_y = 0;
    bool grounded = false;
    bool facing_left = false;
    bool jump_held = false;
    bool walking_active = false;
    int movement_input = 0;
    bool input_up = false;
    bool input_down = false;
    int skid_ticks = 0;
    int skid_dust_cooldown = 0;
    int ground_angle = 0;
    float ground_slope = 0.0F;
    std::vector<DustPuff> dust_puffs;
    AnimationState animation_state = AnimationState::Idle;
    float animation_time = 0.0F;
    float air_time = 0.0F;
    std::size_t animation_frame = 0;

    float x() const { return static_cast<float>(x_raw) / kFixedOne; }
    float y() const { return static_cast<float>(y_raw) / kFixedOne; }
};

int facing_sign(const Player& player);
int signed_ground_speed(const Player& player);
int ground_velocity_x(const Player& player);
int ground_velocity_y(const Player& player);
bool input_matches_facing(const Player& player, int movement);

struct CollisionMask {
    std::vector<unsigned char> pixels;
    std::vector<unsigned char> floor_angles;
    std::vector<unsigned char> plane2_layout;
    std::vector<unsigned char> block_map;
    std::vector<unsigned char> tile_collision;
    std::vector<unsigned char> collision_defs;

    bool solid(int x, int y) const {
        if (x < 0 || x >= kStageWidth || y < 0 || y >= kStageHeight) {
            return true;
        }
        return pixels[static_cast<std::size_t>(y) * kStageWidth + x] != 0;
    }

    int floor_angle(int x, int y) const {
        if (floor_angles.empty()) {
            return 0;
        }
        const int tile_x = std::clamp(x / kCollisionTileSize, 0, kCollisionTilesWide - 1);
        const int tile_y = std::clamp(y / kCollisionTileSize, 0, kCollisionTilesHigh - 1);
        const unsigned char raw =
            floor_angles[static_cast<std::size_t>(tile_y) * kCollisionTilesWide + tile_x];
        return raw >= 0x80 ? static_cast<int>(raw) - 0x100 : static_cast<int>(raw);
    }

    static int read_s8(unsigned char value) {
        return value >= 0x80 ? static_cast<int>(value) - 0x100 : static_cast<int>(value);
    }

    static int read_u16(const std::vector<unsigned char>& bytes, std::size_t offset) {
        if (offset + 1 >= bytes.size()) {
            return 0;
        }
        return static_cast<int>(bytes[offset]) |
            (static_cast<int>(bytes[offset + 1]) << 8);
    }

    std::optional<CollisionResponse> vertical_response(int x, int y) const {
        if (
            plane2_layout.empty() ||
            block_map.empty() ||
            tile_collision.empty() ||
            collision_defs.empty() ||
            x < 0 ||
            x >= kStageWidth ||
            y < 0 ||
            y >= kStageHeight) {
            return std::nullopt;
        }

        const int block_x = x / 32;
        const int block_y = y / 32;
        const std::size_t layout_index =
            static_cast<std::size_t>(block_y * kLevelBlocksWide + block_x) * 2;
        const int block_id = read_u16(plane2_layout, layout_index);

        const int tile_in_block_x = (x & 31) / 8;
        const int tile_in_block_y = (y & 31) / 8;
        const int tile_position = tile_in_block_y * 4 + tile_in_block_x;
        const std::size_t block_offset =
            static_cast<std::size_t>(block_id * 16 + tile_position) * 2;
        const int tile_entry = read_u16(block_map, block_offset);
        const int tile_id = tile_entry & 0x01FF;
        int collision_type = read_u16(
            tile_collision,
            static_cast<std::size_t>(tile_id) * 2);
        if (collision_type == 0xFFFF) {
            collision_type = 0;
        }

        const bool flip_x = (tile_entry & 0x8000) != 0;
        const std::size_t def_offset =
            static_cast<std::size_t>(collision_type * 2 + (flip_x ? 1 : 0)) *
            kCollisionDefSize;
        if (def_offset + kCollisionDefSize > collision_defs.size()) {
            return std::nullopt;
        }

        const int angle = read_s8(collision_defs[def_offset + 1]);
        const int local_x = x & 7;
        const int local_y = y & 7;
        // ROM collision positions are bottom-up, while the viewer stores stage
        // pixels top-down. NSI's height is divisible by 8, so
        // 7 - (rom_y & 7) maps to the viewer's local_y.
        const int table_index = local_y * 8 + (7 - local_x);
        const int response = read_s8(
            collision_defs[def_offset + 2 + 64 + table_index]);
        return CollisionResponse{response, angle};
    }

    int first_solid_y(int x, int start_y, int end_y) const {
        start_y = std::clamp(start_y, 0, kStageHeight - 1);
        end_y = std::clamp(end_y, 0, kStageHeight - 1);
        for (int y = start_y; y <= end_y; ++y) {
            if (solid(x, y)) {
                return y;
            }
        }
        return -1;
    }

    int surface_y_near(int x, int foot_y) const {
        return first_solid_y(
            x,
            foot_y - kGroundProbeUp,
            foot_y + kGroundProbeDown);
    }
};

bool fail(std::string_view message) {
    std::cerr << message << ": " << SDL_GetError() << '\n';
    return false;
}

Texture load_png(SDL_Renderer* renderer, const std::filesystem::path& path,
                 bool black_is_transparent = false) {
    SDL_Surface* surface = SDL_LoadPNG(path.string().c_str());
    if (surface == nullptr) {
        fail("Unable to load " + path.string());
        return {};
    }

    if (black_is_transparent) {
        const Uint32 black = SDL_MapSurfaceRGB(surface, 0, 0, 0);
        if (!SDL_SetSurfaceColorKey(surface, true, black)) {
            fail("Unable to set collision transparency");
            SDL_DestroySurface(surface);
            return {};
        }
    }

    Texture texture{SDL_CreateTextureFromSurface(renderer, surface)};
    SDL_DestroySurface(surface);
    if (texture.value == nullptr) {
        fail("Unable to create texture for " + path.string());
        return {};
    }
    SDL_SetTextureScaleMode(texture.value, SDL_SCALEMODE_NEAREST);
    return texture;
}

SpriteFrame load_sprite_frame(SDL_Renderer* renderer,
                              const std::filesystem::path& path,
                              float origin_x, float origin_y) {
    SpriteFrame frame;
    frame.texture = load_png(renderer, path);
    frame.origin_x = origin_x;
    frame.origin_y = origin_y;
    if (frame.texture.value != nullptr &&
        !SDL_GetTextureSize(frame.texture.value, &frame.width, &frame.height)) {
        fail("Unable to read sprite texture size");
        frame.texture = {};
    }
    return frame;
}

AnimationSequence load_animation_sequence(
    SDL_Renderer* renderer,
    const std::filesystem::path& data_directory,
    std::string_view name,
    const std::vector<float>& durations) {
    AnimationSequence sequence;
    for (std::size_t index = 0; index < durations.size(); ++index) {
        char filename[32]{};
        SDL_snprintf(
            filename,
            sizeof(filename),
            "%.*s_%02zu.png",
            static_cast<int>(name.size()),
            name.data(),
            index);
        sequence.frames.push_back(
            AnimationFrame{
                load_sprite_frame(
                    renderer,
                    data_directory / "sonic" / filename,
                    32.0F,
                    40.0F),
                durations[index],
            });
    }
    return sequence;
}

AnimationSequence load_effect_sequence(
    SDL_Renderer* renderer,
    const std::filesystem::path& data_directory,
    std::string_view name,
    const std::vector<float>& durations,
    const std::vector<std::pair<float, float>>& origins) {
    AnimationSequence sequence;
    for (std::size_t index = 0; index < durations.size(); ++index) {
        char filename[32]{};
        SDL_snprintf(
            filename,
            sizeof(filename),
            "%.*s_%02zu.png",
            static_cast<int>(name.size()),
            name.data(),
            index);
        sequence.frames.push_back(
            AnimationFrame{
                load_sprite_frame(
                    renderer,
                    data_directory / "effects" / filename,
                    origins[index].first,
                    origins[index].second),
                durations[index],
            });
    }
    return sequence;
}

bool animation_sequence_loaded(const AnimationSequence& sequence) {
    return !sequence.frames.empty() &&
        std::all_of(
            sequence.frames.begin(),
            sequence.frames.end(),
            [](const AnimationFrame& frame) {
                return frame.sprite.texture.value != nullptr;
            });
}

CollisionMask load_collision_mask(const std::filesystem::path& path) {
    auto read_binary = [](const std::filesystem::path& binary_path) {
        std::ifstream stream(binary_path, std::ios::binary);
        if (!stream) {
            return std::vector<unsigned char>{};
        }
        return std::vector<unsigned char>(
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>());
    };

    CollisionMask result;
    result.pixels = read_binary(path);
    if (result.pixels.empty()) {
        std::cerr << "Unable to load " << path << '\n';
        return {};
    }
    const auto expected =
        static_cast<std::size_t>(kStageWidth) * kStageHeight;
    if (result.pixels.size() != expected) {
        std::cerr << "Collision mask has " << result.pixels.size()
                  << " bytes; expected " << expected << '\n';
        result.pixels.clear();
    }

    result.floor_angles = read_binary(path.parent_path() / "collision-angle-y.bin");
    if (!result.floor_angles.empty()) {
        const auto expected_angles =
            static_cast<std::size_t>(kCollisionTilesWide) * kCollisionTilesHigh;
        if (result.floor_angles.size() != expected_angles) {
            std::cerr << "Collision angle map has "
                      << result.floor_angles.size() << " bytes; expected "
                      << expected_angles << '\n';
            result.floor_angles.clear();
        }
    }

    result.plane2_layout = read_binary(path.parent_path() / "data" / "plane2.bin");
    result.block_map = read_binary(path.parent_path() / "data" / "blocks.bin");
    result.tile_collision = read_binary(path.parent_path() / "data" / "collision.bin");
    result.collision_defs = read_binary(path.parent_path() / "collision-defs.bin");
    if (
        result.plane2_layout.empty() ||
        result.block_map.empty() ||
        result.tile_collision.empty() ||
        result.collision_defs.empty()) {
        std::cerr << "ROM collision response data is incomplete; "
                     "falling back to collision mask sampling\n";
    }
    return result;
}

std::filesystem::path find_data_directory(
    const std::filesystem::path& requested,
    const std::filesystem::path& executable) {
    if (!requested.empty()) {
        return requested;
    }

    const auto current = std::filesystem::current_path() / "out" / "nsi1";
    if (std::filesystem::is_directory(current)) {
        return current;
    }

    auto directory = std::filesystem::absolute(executable).parent_path();
    for (int depth = 0; depth < 5 && directory.has_parent_path(); ++depth) {
        const auto candidate = directory / "out" / "nsi1";
        if (std::filesystem::is_directory(candidate)) {
            return candidate;
        }
        directory = directory.parent_path();
    }
    return current;
}

void center_camera(float& camera_x, float& camera_y,
                   float& camera_follow_x, const Player& player) {
    camera_follow_x = player.facing_left ? kCameraFollowLeft : kCameraFollowRight;
    camera_x = std::clamp(
        std::floor(player.x()) - camera_follow_x,
        kCameraMinX,
        kCameraMaxX);
    camera_y = std::clamp(
        std::floor(player.y()) - kCameraFollowY + 1.0F,
        kCameraMinY,
        kCameraMaxY);
}

bool set_window_scale(SDL_Window* window, int scale) {
    if (scale < 1 || scale > 6) {
        return true;
    }
    const int width = kLogicalWidth * scale;
    const int height = kLogicalHeight * scale;
    if (!SDL_SetWindowSize(window, width, height)) {
        return fail("Unable to set window size");
    }
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    return true;
}

float normalized_axis(Sint16 value) {
    if (std::abs(static_cast<int>(value)) < kGamepadDeadzone) {
        return 0.0F;
    }
    return static_cast<float>(value) / 32767.0F;
}

void open_gamepad(Application& app, SDL_JoystickID id) {
    if (app.gamepad != nullptr) {
        return;
    }
    app.gamepad = SDL_OpenGamepad(id);
    if (app.gamepad != nullptr) {
        std::cout << "Gamepad: " << SDL_GetGamepadName(app.gamepad) << '\n';
    }
}

void open_first_gamepad(Application& app) {
    int count = 0;
    SDL_JoystickID* gamepads = SDL_GetGamepads(&count);
    if (gamepads != nullptr && count > 0) {
        open_gamepad(app, gamepads[0]);
    }
    SDL_free(gamepads);
}

float approach(float value, float target, float amount) {
    if (value < target) {
        return std::min(value + amount, target);
    }
    return std::max(value - amount, target);
}

std::string_view animation_state_name(AnimationState state) {
    switch (state) {
        case AnimationState::Idle:
            return "idle";
        case AnimationState::Walk:
            return "walk";
        case AnimationState::Run:
            return "run";
        case AnimationState::Peelout:
            return "peelout";
        case AnimationState::Skid:
            return "skid";
        case AnimationState::Jump:
            return "jump";
        case AnimationState::Fall:
            return "fall";
        case AnimationState::Push:
            return "push";
        case AnimationState::LookUp:
            return "look_up";
        case AnimationState::LookDown:
            return "look_down";
        case AnimationState::Balance:
            return "balance";
    }
    return "unknown";
}

AnimationState choose_animation_state(const Player& player) {
    if (!player.grounded && player.air_time >= kAnimationAirThreshold) {
        return player.velocity_y < 0 ? AnimationState::Jump : AnimationState::Fall;
    }
    if (player.input_up && std::abs(player.ground_speed) < 0x100) {
        return AnimationState::LookUp;
    }
    if (player.input_down && std::abs(player.ground_speed) < 0x100) {
        return AnimationState::LookDown;
    }
    if (player.skid_ticks > 0 &&
        player.movement_input != 0 &&
        !input_matches_facing(player, player.movement_input)) {
        return AnimationState::Skid;
    }
    if (std::abs(player.ground_speed) > 0 ||
        std::abs(player.velocity_x) > 0) {
        const int speed = std::abs(player.ground_speed);
        if (speed >= kPeeloutSpeed) {
            return AnimationState::Peelout;
        }
        return speed >= 0x300 ? AnimationState::Run : AnimationState::Walk;
    }
    return AnimationState::Idle;
}

const AnimationSequence& animation_sequence(
    const SonicAnimations& animations, AnimationState state) {
    switch (state) {
        case AnimationState::Idle:
            return animations.idle;
        case AnimationState::Walk:
            return animations.walk;
        case AnimationState::Run:
            return animations.run;
        case AnimationState::Peelout:
            return animations.peelout;
        case AnimationState::Skid:
            return animations.skid;
        case AnimationState::Jump:
            return animations.jump;
        case AnimationState::Fall:
            return animations.fall;
        case AnimationState::Push:
            return animations.push;
        case AnimationState::LookUp:
            return animations.look_up;
        case AnimationState::LookDown:
            return animations.look_down;
        case AnimationState::Balance:
            return animations.balance;
    }
    return animations.idle;
}

void update_animation(
    Player& player, const SonicAnimations& animations, float delta_seconds) {
    const AnimationState next_state = choose_animation_state(player);
    if (next_state != player.animation_state) {
        player.animation_state = next_state;
        player.animation_time = 0.0F;
        player.animation_frame = 0;
    } else {
        player.animation_time += delta_seconds * 60.0F;
    }

    const AnimationSequence& sequence =
        animation_sequence(animations, player.animation_state);
    if (sequence.frames.empty()) {
        player.animation_frame = 0;
        player.animation_time = 0.0F;
        return;
    }

    player.animation_frame %= sequence.frames.size();
    while (player.animation_time >=
           sequence.frames[player.animation_frame].duration_frames) {
        player.animation_time -=
            sequence.frames[player.animation_frame].duration_frames;
        player.animation_frame =
            (player.animation_frame + 1) % sequence.frames.size();
    }
}

void reset_player(Player& player, const CollisionMask& collision) {
    player = {};
    const int surface = collision.first_solid_y(
        static_cast<int>(kPlayerStartX), 0, kStageHeight - 1);
    player.y_raw = static_cast<int>(
        (static_cast<float>(surface >= 0 ? surface : 568) - kPlayerHalfHeight) *
        kFixedOne);
    player.grounded = true;
    player.ground_speed = 0;
    player.velocity_x = 0;
    player.velocity_y = 0;
    player.jump_held = false;
    player.walking_active = false;
    player.animation_state = AnimationState::Idle;
    player.animation_time = 0.0F;
    player.air_time = 0.0F;
    player.animation_frame = 0;
    player.skid_ticks = 0;
}

int approach_fixed(int value, int target, int amount) {
    if (value < target) {
        return std::min(value + amount, target);
    }
    return std::max(value - amount, target);
}

int facing_sign(const Player& player) {
    return player.facing_left ? -1 : 1;
}

float ground_angle_radians(const Player& player) {
    constexpr float pi = 3.14159265358979323846F;
    return static_cast<float>(player.ground_angle) * pi / 128.0F;
}

int signed_ground_speed(const Player& player) {
    return facing_sign(player) * player.ground_speed;
}

int ground_velocity_x(const Player& player) {
    const float projected =
        std::cos(ground_angle_radians(player)) *
        static_cast<float>(signed_ground_speed(player));
    return static_cast<int>(std::round(projected));
}

int ground_velocity_y(const Player& player) {
    const float projected =
        std::sin(ground_angle_radians(player)) *
        static_cast<float>(signed_ground_speed(player));
    return static_cast<int>(std::round(projected));
}

bool input_matches_facing(const Player& player, int movement) {
    return movement == facing_sign(player);
}

void flip_player_facing(Player& player) {
    player.facing_left = !player.facing_left;
}

void spawn_skid_dust(Player& player) {
    player.dust_puffs.push_back(DustPuff{
        player.x(),
        player.y() + kSkidDustFootOffsetY,
        kSkidDustTicks,
    });
}

void update_dust_puffs(Player& player) {
    for (DustPuff& puff : player.dust_puffs) {
        --puff.ticks;
    }
    player.dust_puffs.erase(
        std::remove_if(
            player.dust_puffs.begin(),
            player.dust_puffs.end(),
            [](const DustPuff& puff) { return puff.ticks <= 0; }),
        player.dust_puffs.end());
}

int signed_angle_to_ground_force(int angle) {
    constexpr float pi = 3.14159265358979323846F;
    const float radians = static_cast<float>(angle) * pi / 128.0F;
    return static_cast<int>(std::round(std::sin(radians) * kSlopeGravity));
}

int rom_y_to_view_y(int rom_y) {
    return kStageHeight - 1 - rom_y;
}

int view_y_to_rom_y(int view_y) {
    return kStageHeight - 1 - view_y;
}

std::optional<CollisionResponse> vertical_response_rom_y(
    const CollisionMask& collision, int x, int rom_y) {
    return collision.vertical_response(x, rom_y_to_view_y(rom_y));
}

RomCollisionHit rom_bg_coll_chk4(
    const CollisionMask& collision, int x, int rom_y, int scan_length) {
    int offset = 0;
    int remaining = scan_length;
    int probe_y = rom_y;
    while (remaining > 0) {
        const auto sample = vertical_response_rom_y(collision, x, probe_y);
        if (sample.has_value() && sample->response != 0x7F) {
            if (sample->response > 0) {
                return RomCollisionHit{
                    true,
                    sample->response + offset,
                    sample->angle,
                };
            }
        }
        --remaining;
        ++probe_y;
        ++offset;
    }
    return {};
}

RomCollisionHit rom_bg_coll_chk3(
    const CollisionMask& collision, int x, int rom_y, int scan_length) {
    int offset = 0;
    int remaining = scan_length;
    int probe_y = rom_y;
    while (remaining > 0) {
        const auto sample = vertical_response_rom_y(collision, x, probe_y);
        if (sample.has_value() && sample->response != 0x7F) {
            if (sample->response < 0) {
                return RomCollisionHit{
                    true,
                    sample->response + offset,
                    sample->angle,
                };
            }
        }
        --remaining;
        --probe_y;
        --offset;
    }
    return {};
}

RomCollisionHit choose_bg_coll_chk4_pair(
    const CollisionMask& collision, int center_x, int rom_y, int scan_length) {
    const RomCollisionHit right =
        rom_bg_coll_chk4(collision, center_x + kSlopeProbeRadius, rom_y, scan_length);
    const RomCollisionHit left =
        rom_bg_coll_chk4(collision, center_x - kSlopeProbeRadius, rom_y, scan_length);
    if (!left.hit) {
        return right;
    }
    if (!right.hit) {
        return left;
    }
    return left.delta_y >= right.delta_y ? left : right;
}

RomCollisionHit choose_bg_coll_chk3_pair(
    const CollisionMask& collision, int center_x, int rom_y, int scan_length) {
    const RomCollisionHit right =
        rom_bg_coll_chk3(collision, center_x + kSlopeProbeRadius, rom_y, scan_length);
    const RomCollisionHit left =
        rom_bg_coll_chk3(collision, center_x - kSlopeProbeRadius, rom_y, scan_length);
    if (!left.hit) {
        return right;
    }
    if (!right.hit) {
        return left;
    }
    return left.delta_y <= right.delta_y ? left : right;
}

bool update_ground_contact(Player& player, const CollisionMask& collision) {
    const int center_x = static_cast<int>(std::round(player.x()));
    const int center_y = static_cast<int>(std::round(player.y()));
    const int rom_center_y = view_y_to_rom_y(center_y);
    const int rom_floor_probe_y = rom_center_y - static_cast<int>(kPlayerHalfHeight);
    const int rom_ceiling_probe_y = rom_center_y + static_cast<int>(kPlayerHalfHeight);

    constexpr int kRomGroundScanLength = 0x10;
    const RomCollisionHit floor_hit =
        choose_bg_coll_chk4_pair(collision, center_x, rom_floor_probe_y, kRomGroundScanLength);
    const RomCollisionHit ceiling_hit =
        choose_bg_coll_chk3_pair(collision, center_x, rom_ceiling_probe_y, kRomGroundScanLength);

    RomCollisionHit selected = floor_hit;
    if (!selected.hit) {
        selected = ceiling_hit;
    }
    if (!selected.hit) {
        return false;
    }

    const int new_center_y = center_y - selected.delta_y;
    player.y_raw = new_center_y * kFixedOne;
    player.velocity_y = 0;
    player.air_time = 0.0F;
    player.ground_angle = selected.angle;

    const int left_surface = rom_y_to_view_y(
        rom_floor_probe_y + rom_bg_coll_chk4(
            collision,
            center_x - kSlopeProbeRadius,
            rom_floor_probe_y,
            kRomGroundScanLength).delta_y);
    const int right_surface = rom_y_to_view_y(
        rom_floor_probe_y + rom_bg_coll_chk4(
            collision,
            center_x + kSlopeProbeRadius,
            rom_floor_probe_y,
            kRomGroundScanLength).delta_y);
    player.ground_slope =
        static_cast<float>(right_surface - left_surface) /
        static_cast<float>(kSlopeProbeRadius * 2);
    return true;
}

void update_player(Player& player, const CollisionMask& collision,
                   int movement, bool jump_pressed, bool jump_held,
                   bool input_up, bool input_down) {
    player.movement_input = movement;
    player.jump_held = jump_held;
    player.input_up = input_up;
    player.input_down = input_down;

    if (player.grounded) {
        const int slope_force = signed_angle_to_ground_force(player.ground_angle);
        if (slope_force != 0) {
            int signed_speed = signed_ground_speed(player) + slope_force;
            signed_speed = std::clamp(signed_speed, -kGroundMaxSpeed, kGroundMaxSpeed);
            if (signed_speed != 0) {
                player.facing_left = signed_speed < 0;
                player.ground_speed = std::abs(signed_speed);
            }
        }

        if (movement != 0) {
            player.walking_active = true;
            if (player.ground_speed == 0) {
                player.facing_left = movement < 0;
            }

            if (input_matches_facing(player, movement)) {
                player.ground_speed = std::min(
                    player.ground_speed + kGroundAcceleration, kGroundMaxSpeed);
                player.skid_dust_cooldown = 0;
            } else {
                const bool runtime_skid = player.ground_speed > 0x300;
                if (runtime_skid && player.skid_ticks == 0) {
                    player.skid_ticks = kSkidAnimationTicks;
                }
                if (runtime_skid && player.skid_dust_cooldown <= 0) {
                    spawn_skid_dust(player);
                    player.skid_dust_cooldown = kSkidDustSpawnInterval;
                }
                player.ground_speed -= kGroundSkidDeceleration;
                if (player.ground_speed < 0) {
                    player.ground_speed = -player.ground_speed;
                    flip_player_facing(player);
                    player.skid_ticks = 0;
                    player.skid_dust_cooldown = 0;
                }
            }
        } else {
            player.ground_speed =
                approach_fixed(player.ground_speed, 0, kGroundFriction);
            player.skid_dust_cooldown = 0;
            if (std::abs(player.ground_speed) < 0x100) {
                player.ground_speed = 0;
                player.walking_active = false;
            }
        }
        player.velocity_x = ground_velocity_x(player);
        player.velocity_y = ground_velocity_y(player);
        if (player.skid_ticks > 0) {
            --player.skid_ticks;
        }
        if (player.skid_dust_cooldown > 0) {
            --player.skid_dust_cooldown;
        }
    } else {
        player.skid_dust_cooldown = 0;
        if (movement > 0) {
            player.velocity_x = std::min(
                player.velocity_x + kAirAcceleration, kAirMaxXSpeed);
            player.facing_left = false;
        } else if (movement < 0) {
            player.velocity_x = std::max(
                player.velocity_x - kAirAcceleration, -kAirMaxXSpeed);
            player.facing_left = true;
        }
    }

    if (jump_pressed && player.grounded) {
        player.velocity_y = -kJumpImpulse;
        player.velocity_x = ground_velocity_x(player);
        player.grounded = false;
        player.walking_active = false;
        player.air_time = 0.0F;
    }

    if (!player.grounded && !jump_held && player.velocity_y < -kJumpReleaseLimit) {
        player.velocity_y = -kJumpReleaseLimit;
    }

    if (!player.grounded) {
        player.velocity_y = std::min(player.velocity_y + kGravity, kFallMaxSpeed);
    }

    player.x_raw += player.velocity_x;
    player.x_raw = std::clamp(
        player.x_raw,
        static_cast<int>((kCameraMinX + kPlayerHalfWidth) * kFixedOne),
        static_cast<int>((kCameraMaxX + kLogicalWidth - kPlayerHalfWidth) * kFixedOne));

    player.y_raw += player.velocity_y;

    if (player.grounded) {
        player.grounded = update_ground_contact(player, collision);
    } else if (player.velocity_y >= 0 && update_ground_contact(player, collision)) {
        player.grounded = true;
        player.facing_left = player.velocity_x < 0;
        player.ground_speed = std::abs(player.velocity_x);
        player.walking_active = player.ground_speed != 0;
    }

    if (!player.grounded) {
        player.ground_slope = 0.0F;
        player.ground_angle = 0;
        player.air_time += 1.0F / 60.0F;
    }
    update_dust_puffs(player);
}

void update_camera(float& camera_x, float& camera_y, float& camera_follow_x,
                   const Player& player) {
    const float target_follow_x =
        player.facing_left ? kCameraFollowLeft : kCameraFollowRight;
    camera_follow_x = approach(
        camera_follow_x, target_follow_x, kCameraFollowStep);
    const float target_x = std::clamp(
        std::floor(player.x()) - camera_follow_x,
        kCameraMinX,
        kCameraMaxX);
    const float target_y = std::clamp(
        std::floor(player.y()) - kCameraFollowY + 1.0F,
        kCameraMinY,
        kCameraMaxY);
    camera_x = target_x;
    camera_y = target_y;
}

const SpriteFrame& select_animation_frame(
    const Player& player, const SonicAnimations& animations) {
    const AnimationSequence& sequence =
        animation_sequence(animations, player.animation_state);
    if (!sequence.frames.empty()) {
        return sequence
            .frames[player.animation_frame % sequence.frames.size()]
            .sprite;
    }
    return animations.idle.frames[0].sprite;
}

bool render_frame(Application& app, SDL_Texture* stage, SDL_Texture* collision,
                  const AnimationSequence& skid_dust,
                  const SpriteFrame& sonic, const Player& player,
                  float camera_x, float camera_y, bool show_collision) {
    SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, 255);
    SDL_RenderClear(app.renderer);

    SDL_FRect source{
        std::floor(camera_x),
        std::floor(camera_y),
        static_cast<float>(kLogicalWidth),
        static_cast<float>(kLogicalHeight),
    };
    if (!SDL_RenderTexture(app.renderer, stage, &source, nullptr)) {
        return fail("Unable to render stage");
    }

    if (show_collision) {
        SDL_SetTextureAlphaMod(collision, 160);
        if (!SDL_RenderTexture(app.renderer, collision, &source, nullptr)) {
            return fail("Unable to render collision overlay");
        }
    }

    for (const DustPuff& puff : player.dust_puffs) {
        if (!skid_dust.frames.empty()) {
            const int age = kSkidDustTicks - puff.ticks;
            const std::size_t frame_index = std::min<std::size_t>(
                static_cast<std::size_t>(age / 2),
                skid_dust.frames.size() - 1);
            const SpriteFrame& dust = skid_dust.frames[frame_index].sprite;
            SDL_FRect dust_destination{
                std::floor(puff.x - camera_x - dust.origin_x),
                std::floor(puff.y - camera_y - dust.origin_y),
                dust.width,
                dust.height,
            };
            if (!SDL_RenderTexture(
                    app.renderer,
                    dust.texture.value,
                    nullptr,
                    &dust_destination)) {
                return fail("Unable to render ROM skid dust");
            }
        }
    }

    SDL_FRect sonic_destination{
        std::floor(player.x() - camera_x - sonic.origin_x),
        std::floor(player.y() - camera_y - sonic.origin_y),
        sonic.width,
        sonic.height,
    };
    if (!SDL_RenderTextureRotated(
            app.renderer,
            sonic.texture.value,
            nullptr,
            &sonic_destination,
            0.0,
            nullptr,
            player.facing_left ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE)) {
        return fail("Unable to render Sonic");
    }

    SDL_RenderPresent(app.renderer);
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    bool smoke_test = false;
    std::filesystem::path requested_data;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--smoke-test") {
            smoke_test = true;
        } else if (requested_data.empty()) {
            requested_data = argv[index];
        } else {
            std::cerr << "Usage: sonic-pocket-viewer [data-directory] [--smoke-test]\n";
            return 2;
        }
    }

    const auto data_directory =
        find_data_directory(requested_data, argc > 0 ? argv[0] : "");
    const auto stage_path = data_directory / "stage.png";
    const auto collision_path = data_directory / "collision.png";
    const auto collision_mask_path = data_directory / "collision-mask.bin";
    const auto sonic_idle_path = data_directory / "sonic" / "idle.png";
    if (!std::filesystem::is_regular_file(stage_path) ||
        !std::filesystem::is_regular_file(collision_path) ||
        !std::filesystem::is_regular_file(collision_mask_path) ||
        !std::filesystem::is_regular_file(sonic_idle_path)) {
        std::cerr << "Missing extracted stage data in " << data_directory << '\n'
                  << "Run: py -3 tools/extract_level.py\n";
        return 2;
    }

    Application app;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        fail("Unable to initialize SDL");
        return 1;
    }

    const SDL_WindowFlags flags =
        smoke_test ? SDL_WINDOW_HIDDEN : SDL_WINDOW_RESIZABLE;
    if (!SDL_CreateWindowAndRenderer(
            "Sonic Pocket - Native Stage Viewer",
            kLogicalWidth * kDefaultWindowScale,
            kLogicalHeight * kDefaultWindowScale,
            flags,
            &app.window,
            &app.renderer)) {
        fail("Unable to create the SDL window");
        return 1;
    }
    if (!SDL_SetRenderLogicalPresentation(
            app.renderer,
            kLogicalWidth,
            kLogicalHeight,
            SDL_LOGICAL_PRESENTATION_INTEGER_SCALE)) {
        fail("Unable to set integer-scaled logical presentation");
        return 1;
    }
    SDL_SetRenderVSync(app.renderer, 1);

    Texture stage = load_png(app.renderer, stage_path);
    Texture collision = load_png(app.renderer, collision_path, true);
    SonicAnimations sonic_animations{
        load_animation_sequence(
            app.renderer,
            data_directory,
            "idle",
            {60.0F, 3.0F, 20.0F, 30.0F, 10.0F, 10.0F,
             10.0F, 10.0F, 10.0F, 10.0F, 10.0F, 10.0F,
             10.0F, 10.0F, 10.0F, 10.0F, 35.0F, 20.0F}),
        load_animation_sequence(
            app.renderer,
            data_directory,
            "walk",
            {4.0F, 4.0F, 4.0F, 4.0F, 4.0F, 4.0F, 4.0F, 4.0F}),
        load_animation_sequence(
            app.renderer,
            data_directory,
            "run",
            {2.0F, 2.0F, 2.0F, 2.0F, 2.0F, 2.0F, 2.0F, 2.0F}),
        load_animation_sequence(
            app.renderer,
            data_directory,
            "peelout",
            {1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F}),
        load_animation_sequence(app.renderer, data_directory, "skid", {20.0F, 3.0F}),
        load_animation_sequence(
            app.renderer,
            data_directory,
            "jump",
            {1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F}),
        load_animation_sequence(
            app.renderer,
            data_directory,
            "fall",
            {1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F}),
        load_animation_sequence(app.renderer, data_directory, "push", {12.0F, 12.0F, 12.0F, 12.0F}),
        load_animation_sequence(app.renderer, data_directory, "look_up", {10.0F, 10.0F, 10.0F}),
        load_animation_sequence(app.renderer, data_directory, "look_down", {4.0F, 4.0F, 4.0F}),
        load_animation_sequence(app.renderer, data_directory, "balance", {10.0F, 10.0F}),
    };
    AnimationSequence skid_dust = load_effect_sequence(
        app.renderer,
        data_directory,
        "skid_dust",
        {2.0F, 2.0F, 2.0F, 2.0F},
        {{3.0F, 4.0F}, {4.0F, 4.0F}, {3.0F, 4.0F}, {4.0F, 4.0F}});
    CollisionMask collision_mask = load_collision_mask(collision_mask_path);
    if (stage.value == nullptr || collision.value == nullptr ||
        !animation_sequence_loaded(sonic_animations.idle) ||
        !animation_sequence_loaded(sonic_animations.walk) ||
        !animation_sequence_loaded(sonic_animations.run) ||
        !animation_sequence_loaded(sonic_animations.peelout) ||
        !animation_sequence_loaded(sonic_animations.skid) ||
        !animation_sequence_loaded(sonic_animations.jump) ||
        !animation_sequence_loaded(sonic_animations.fall) ||
        !animation_sequence_loaded(sonic_animations.push) ||
        !animation_sequence_loaded(sonic_animations.look_up) ||
        !animation_sequence_loaded(sonic_animations.look_down) ||
        !animation_sequence_loaded(sonic_animations.balance) ||
        !animation_sequence_loaded(skid_dust) ||
        collision_mask.pixels.empty()) {
        return 1;
    }

    Player player;
    reset_player(player, collision_mask);
    float camera_x = 0.0F;
    float camera_y = 0.0F;
    float camera_follow_x = kCameraFollowRight;
    center_camera(camera_x, camera_y, camera_follow_x, player);

    if (smoke_test) {
        if (!render_frame(
                app, stage.value, collision.value,
                skid_dust,
                sonic_animations.idle.frames[0].sprite, player,
                camera_x, camera_y, true)) {
            return 1;
        }
        std::cout << "Viewer smoke test passed using " << data_directory << '\n';
        return 0;
    }

    open_first_gamepad(app);
    bool running = true;
    bool show_collision = false;
    bool jump_pressed = false;
    Uint64 previous_ticks = SDL_GetTicks();
    float simulation_accumulator = 0.0F;
    constexpr float kFixedFrameSeconds = 1.0F / 30.0F;
    constexpr float kAnimationTickSeconds = 1.0F / 60.0F;

    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    running = false;
                    break;
                case SDL_EVENT_KEY_DOWN:
                    if (event.key.repeat) {
                        break;
                    }
                    if (event.key.key == SDLK_ESCAPE) {
                        running = false;
                    } else if (event.key.key == SDLK_C) {
                        show_collision = !show_collision;
                    } else if (event.key.key == SDLK_R ||
                               event.key.key == SDLK_HOME) {
                        reset_player(player, collision_mask);
                        center_camera(camera_x, camera_y, camera_follow_x, player);
                    } else if (event.key.key == SDLK_SPACE ||
                               event.key.key == SDLK_Z) {
                        jump_pressed = true;
                    } else if (event.key.key >= SDLK_1 &&
                               event.key.key <= SDLK_6) {
                        if (!set_window_scale(
                                app.window,
                                static_cast<int>(event.key.key - SDLK_0))) {
                            return 1;
                        }
                    }
                    break;
                case SDL_EVENT_GAMEPAD_ADDED:
                    open_gamepad(app, event.gdevice.which);
                    break;
                case SDL_EVENT_GAMEPAD_REMOVED:
                    if (app.gamepad != nullptr &&
                        SDL_GetGamepadID(app.gamepad) == event.gdevice.which) {
                        SDL_CloseGamepad(app.gamepad);
                        app.gamepad = nullptr;
                    }
                    break;
                case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                    if (event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) {
                        jump_pressed = true;
                    } else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST) {
                        show_collision = !show_collision;
                    } else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_BACK) {
                        reset_player(player, collision_mask);
                        center_camera(camera_x, camera_y, camera_follow_x, player);
                    }
                    break;
                default:
                    break;
            }
        }

        const Uint64 current_ticks = SDL_GetTicks();
        const float delta_seconds = std::min(
            static_cast<float>(current_ticks - previous_ticks) / 1000.0F,
            0.05F);
        previous_ticks = current_ticks;
        simulation_accumulator += delta_seconds;

        const bool* keyboard = SDL_GetKeyboardState(nullptr);
        int movement_x = 0;
        movement_x += keyboard[SDL_SCANCODE_RIGHT] || keyboard[SDL_SCANCODE_D];
        movement_x -= keyboard[SDL_SCANCODE_LEFT] || keyboard[SDL_SCANCODE_A];
        bool jump_held =
            keyboard[SDL_SCANCODE_SPACE] || keyboard[SDL_SCANCODE_Z];
        bool input_up = keyboard[SDL_SCANCODE_UP] || keyboard[SDL_SCANCODE_W];
        bool input_down = keyboard[SDL_SCANCODE_DOWN] || keyboard[SDL_SCANCODE_S];

        if (app.gamepad != nullptr) {
            const float analog_x = normalized_axis(SDL_GetGamepadAxis(
                app.gamepad, SDL_GAMEPAD_AXIS_LEFTX));
            movement_x += analog_x > 0.35F;
            movement_x -= analog_x < -0.35F;
            movement_x += SDL_GetGamepadButton(
                app.gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
            movement_x -= SDL_GetGamepadButton(
                app.gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
            input_up = input_up ||
                SDL_GetGamepadButton(app.gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP);
            input_down = input_down ||
                SDL_GetGamepadButton(app.gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
            jump_held = jump_held ||
                SDL_GetGamepadButton(app.gamepad, SDL_GAMEPAD_BUTTON_SOUTH);
        }

        int simulation_steps = 0;
        while (simulation_accumulator >= kFixedFrameSeconds &&
               simulation_steps < 4) {
            update_player(
                player, collision_mask,
                std::clamp(movement_x, -1, 1),
                jump_pressed,
                jump_held,
                input_up,
                input_down);
            jump_pressed = false;
            update_animation(player, sonic_animations, kAnimationTickSeconds);
            update_camera(camera_x, camera_y, camera_follow_x, player);
            simulation_accumulator -= kFixedFrameSeconds;
            ++simulation_steps;
        }
        if (simulation_steps == 4) {
            simulation_accumulator = 0.0F;
        }
        const SpriteFrame& sonic_frame = select_animation_frame(
            player, sonic_animations);

        const std::string title =
            "Sonic Pocket - Camera " +
            std::to_string(static_cast<int>(camera_x)) + ", " +
            std::to_string(static_cast<int>(camera_y)) + " - Sonic " +
            std::to_string(static_cast<int>(player.x())) + ", " +
            std::to_string(static_cast<int>(player.y())) + " - gs " +
            std::to_string(player.ground_speed) + " vx " +
            std::to_string(player.velocity_x) + " vy " +
            std::to_string(player.velocity_y) + " - " +
            std::string(animation_state_name(player.animation_state)) +
            (player.grounded ? " grounded" : " air") +
            (show_collision ? " - Collision ON" : "");
        SDL_SetWindowTitle(app.window, title.c_str());

        if (!render_frame(
                app,
                stage.value,
                collision.value,
                skid_dust,
                sonic_frame,
                player,
                camera_x,
                camera_y,
                show_collision)) {
            return 1;
        }
    }

    return 0;
}
