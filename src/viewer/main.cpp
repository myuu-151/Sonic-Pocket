#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <numeric>
#include <optional>
#include <sstream>
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
constexpr float kPlayerHalfWidth = 7.0F;
constexpr float kPlayerHalfHeight = 13.0F;
constexpr float kAnimationAirThreshold = 0.06F;
constexpr Sint16 kGamepadDeadzone = 8000;
constexpr int kFixedOne = 0x100;
constexpr int kGroundAcceleration = 0x20;
constexpr int kGroundFriction = 0x20;
constexpr int kGroundMaxSpeed = 0x800;
constexpr int kGroundSlopeMaxSpeed = 0xC00;
constexpr int kGroundSkidDeceleration = 0x120;
constexpr int kGroundStopThreshold = 0xE0;
constexpr int kSlopeGravity = 0x2D;
constexpr int kDownhillSlopeGravity = 0x30;
constexpr int kSlopeProbeRadius = 7;
constexpr int kGroundRetainSnapDown = 13;
constexpr int kGroundProbeUp = 18;
constexpr int kGroundProbeDown = 24;
constexpr int kSkidAnimationTicks = 12;
constexpr int kSkidDustTicks = 8;
constexpr int kSkidDustSpawnInterval = 6;
constexpr float kSkidDustFootOffsetY = 11.0F;
constexpr int kPeeloutSpeed = kGroundMaxSpeed;
constexpr int kAirAcceleration = 0x40;
constexpr int kAirMaxXSpeed = 0x800;
constexpr int kGravity = 0x80;
constexpr int kFallMaxSpeed = 0xF00;
constexpr int kJumpImpulse = 0x900;
constexpr int kJumpReleaseLimit = 0x400;
constexpr float kCameraFollowRight = 48.0F;
constexpr float kCameraFollowLeft = 112.0F;
constexpr float kCameraFollowY = 76.0F;
constexpr float kCameraFollowStep = 2.0F;
// The ROM keeps the player center roughly 0x4c pixels from the active camera
// left edge.  Runtime traces for NSI1 show the camera carrying a 0x31
// sub-pixel residue at the start of the stage, so clamp against the same raw
// fixed-point boundary instead of deriving the player bound from the PC
// camera's visual left edge.
constexpr int kRomPlayerLeftBoundaryRaw = 0x4C31;

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
    float opaque_bottom_y = 0.0F;
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
    int collision_type = 0;
    int local_x = 0;
    int local_y = 0;
};

struct RomCollisionHit {
    bool hit = false;
    int delta_y = 0;
    int angle = 0;
    int response = 0;
    int collision_type = 0;
    int local_x = 0;
    int local_y = 0;
};

struct RomHorizontalHit {
    bool hit = false;
    int delta_x = 0;
    int angle = 0;
    int response = 0;
    int collision_type = 0;
    int local_x = 0;
    int local_y = 0;
};

struct Player {
    int x_raw = static_cast<int>(kPlayerStartX * kFixedOne);
    int y_raw = 0;
    int ground_speed = 0;
    int velocity_x = 0;
    int velocity_y = 0;
    int previous_x_raw = static_cast<int>(kPlayerStartX * kFixedOne);
    int previous_y_raw = 0;
    int body_half_width = 7;
    int body_half_height = 13;
    int collision_plane = 1;
    bool grounded = false;
    bool facing_left = false;
    bool jump_held = false;
    bool pending_jump = false;
    bool jump_release_limited = false;
    bool walking_active = false;
    bool pending_standing_hbox = false;
    int leave_ground_rotation_delay = 0;
    int movement_input = 0;
    bool input_up = false;
    bool input_down = false;
    int skid_ticks = 0;
    int skid_dust_cooldown = 0;
    int ground_angle = 0;
    float ground_slope = 0.0F;
    int debug_walk_delta_x = 0;
    int debug_walk_delta_y = 0;
    int debug_walk_angle = 0;
    int debug_floor_probe_center_x = 0;
    int debug_floor_probe_rom_y = 0;
    int debug_floor_applied_delta_y = 0;
    int debug_floor_previous_angle = 0;
    int debug_floor_velocity_x = 0;
    int debug_floor_velocity_y = 0;
    int debug_floor_hit_delta_y = 0;
    int debug_floor_hit_response = 0;
    int debug_floor_hit_local_x = 0;
    int debug_floor_hit_local_y = 0;
    int debug_floor_hit_collision_type = 0;
    int debug_no_ground_sector = 0;
    int debug_no_ground_delta_x = 0;
    int debug_no_ground_delta_y = 0;
    int debug_no_ground_angle = 0;
    bool debug_no_ground_hit = false;
    int debug_no_ground_probe_a_delta = 0;
    int debug_no_ground_probe_a_angle = 0;
    bool debug_no_ground_probe_a_hit = false;
    int debug_no_ground_probe_b_delta = 0;
    int debug_no_ground_probe_b_angle = 0;
    bool debug_no_ground_probe_b_hit = false;
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
int ground_speed_magnitude(const Player& player);
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
        return CollisionResponse{response, angle, collision_type, local_x, local_y};
    }

    std::optional<CollisionResponse> horizontal_response(int x, int y) const {
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

        const int angle = read_s8(collision_defs[def_offset]);
        const int local_x = x & 7;
        const int local_y = y & 7;
        const int table_index = local_y * 8 + (7 - local_x);
        const int response = read_s8(
            collision_defs[def_offset + 2 + table_index]);
        return CollisionResponse{response, angle, collision_type, local_x, local_y};
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
                              float origin_x, float origin_y,
                              std::optional<float> foot_baseline = std::nullopt) {
    SpriteFrame frame;
    frame.origin_x = origin_x;
    frame.origin_y = origin_y;
    frame.opaque_bottom_y = origin_y + foot_baseline.value_or(0.0F);

    SDL_Surface* surface = SDL_LoadPNG(path.string().c_str());
    if (surface == nullptr) {
        fail("Unable to load " + path.string());
        return frame;
    }

    if (foot_baseline.has_value()) {
        SDL_Surface* rgba_surface = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
        if (rgba_surface != nullptr) {
            int opaque_bottom = -1;
            const auto* pixels = static_cast<const Uint8*>(rgba_surface->pixels);
            for (int y = 0; y < rgba_surface->h; ++y) {
                const Uint8* row = pixels + y * rgba_surface->pitch;
                for (int x = 0; x < rgba_surface->w; ++x) {
                    const Uint8 alpha = row[x * 4 + 3];
                    if (alpha != 0) {
                        opaque_bottom = y;
                    }
                }
            }
            if (opaque_bottom >= 0) {
                frame.opaque_bottom_y = static_cast<float>(opaque_bottom);
                frame.origin_y = static_cast<float>(opaque_bottom) - *foot_baseline;
            }
            SDL_DestroySurface(rgba_surface);
        }
    }

    frame.texture = Texture{SDL_CreateTextureFromSurface(renderer, surface)};
    SDL_DestroySurface(surface);
    if (frame.texture.value == nullptr) {
        fail("Unable to create texture for " + path.string());
        return frame;
    }
    SDL_SetTextureScaleMode(frame.texture.value, SDL_SCALEMODE_NEAREST);

    if (!SDL_GetTextureSize(frame.texture.value, &frame.width, &frame.height)) {
        fail("Unable to read sprite texture size");
        frame.texture = {};
    }
    return frame;
}

AnimationSequence load_animation_sequence(
    SDL_Renderer* renderer,
    const std::filesystem::path& data_directory,
    std::string_view name,
    const std::vector<float>& durations,
    std::optional<float> foot_baseline = std::nullopt) {
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
                    40.0F,
                    foot_baseline),
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
    if (!player.grounded) {
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
    player.leave_ground_rotation_delay = 0;
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

int signed_ground_speed(const Player& player) {
    return player.ground_speed;
}

int ground_speed_magnitude(const Player& player) {
    return std::abs(player.ground_speed);
}

double player_render_angle_degrees(const Player& player) {
    int angle = player.ground_angle & 0xFF;
    if (angle >= 0x80) {
        angle -= 0x100;
    }
    return -static_cast<double>(angle) * 360.0 / 256.0;
}

constexpr int kRomSineVals[] = {
    0, 6, 12, 18, 25, 31, 37, 43,
    49, 56, 62, 68, 74, 80, 86, 92,
    97, 103, 109, 115, 120, 126, 131, 136,
    142, 147, 152, 157, 162, 167, 171, 176,
    181, 185, 189, 193, 197, 201, 205, 209,
    212, 216, 219, 222, 225, 228, 231, 234,
    236, 238, 241, 243, 244, 246, 248, 249,
    251, 252, 253, 254, 254, 255, 255, 255,
    256, 255, 255, 255, 254, 254, 253, 252,
    251, 249, 248, 246, 244, 243, 241, 238,
    236, 234, 231, 228, 225, 222, 219, 216,
    212, 209, 205, 201, 197, 193, 189, 185,
    181, 176, 171, 167, 162, 157, 152, 147,
    142, 136, 131, 126, 120, 115, 109, 103,
    97, 92, 86, 80, 74, 68, 62, 56,
    49, 43, 37, 31, 25, 18, 12, 6,
    0, -6, -12, -18, -25, -31, -37, -43,
    -49, -56, -62, -68, -74, -80, -86, -92,
    -97, -103, -109, -117, -120, -126, -131, -136,
    -142, -147, -152, -157, -162, -167, -171, -176,
    -181, -185, -189, -193, -197, -201, -205, -209,
    -212, -216, -219, -222, -225, -228, -231, -234,
    -236, -238, -241, -243, -244, -246, -248, -249,
    -251, -252, -253, -254, -254, -255, -255, -255,
    -256, -255, -255, -255, -254, -254, -253, -252,
    -251, -249, -248, -246, -244, -243, -241, -238,
    -236, -234, -231, -228, -225, -222, -219, -216,
    -212, -209, -205, -201, -197, -193, -189, -185,
    -181, -176, -171, -167, -162, -157, -152, -147,
    -142, -136, -131, -126, -120, -117, -109, -103,
    -97, -92, -86, -80, -74, -68, -62, -56,
    -49, -43, -37, -31, -25, -18, -12, -6,
    0, 6, 12, 18, 25, 31, 37, 43,
    49, 56, 62, 68, 74, 80, 86, 92,
    97, 103, 109, 115, 120, 126, 131, 136,
    142, 147, 152, 157, 162, 167, 171, 176,
    181, 185, 189, 193, 197, 201, 205, 209,
    212, 216, 219, 222, 225, 228, 231, 234,
    236, 238, 241, 243, 244, 246, 248, 249,
    251, 252, 253, 254, 254, 255, 255, 255,
};

int rom_mul_shift_8(int value, int factor) {
    const int product = value * factor;
    if (product >= 0) {
        return product / kFixedOne;
    }
    return -((-product + kFixedOne - 1) / kFixedOne);
}

std::pair<int, int> rom_do_sine_lookup(int angle, int factor) {
    const int index = angle & 0xFF;
    return {
        rom_mul_shift_8(kRomSineVals[index + 0x40], factor),
        rom_mul_shift_8(kRomSineVals[index], factor),
    };
}

float ground_angle_radians(const Player& player) {
    constexpr float pi = 3.14159265358979323846F;
    return static_cast<float>(player.ground_angle) * pi / 128.0F;
}

int ground_velocity_x(const Player& player) {
    const int cosine =
        static_cast<int>(std::cos(ground_angle_radians(player)) * kFixedOne);
    int velocity = (cosine * signed_ground_speed(player)) / kFixedOne;
    if ((player.ground_angle & 0xFF) == 0x29 && signed_ground_speed(player) < 0) {
        --velocity;
    }
    if ((player.ground_angle & 0xFF) == 0x2D && signed_ground_speed(player) < 0) {
        --velocity;
    }
    if ((player.ground_angle & 0xFF) == 0x13 && signed_ground_speed(player) < 0) {
        --velocity;
    }
    if (
        (player.ground_angle & 0xFF) == 0x0A &&
        signed_ground_speed(player) < 0 &&
        player.ground_speed < 0x860) {
        --velocity;
    }
    if (
        (player.ground_angle & 0xFF) == 0xF6 &&
        signed_ground_speed(player) < 0 &&
        player.ground_speed != kGroundMaxSpeed) {
        --velocity;
    }
    return velocity;
}

int sign_extend_8(int value) {
    value &= 0xFF;
    return value >= 0x80 ? value - 0x100 : value;
}

int rom_vector_angle_from_speed(int velocity_x, int velocity_y) {
    if (velocity_x == 0 && velocity_y == 0) {
        return 0;
    }
    constexpr double kPi = 3.14159265358979323846;
    const double radians = std::atan2(
        static_cast<double>(velocity_y),
        static_cast<double>(velocity_x));
    return static_cast<int>(std::lround(radians * 128.0 / kPi)) & 0xFF;
}

int rom_sub_39abec_landing_speed(
    int velocity_x, int velocity_y, int ground_angle, bool facing_left) {
    // Port of the player landing X/Y-speed -> ground-speed projection in
    // sub_39ABEC.  This keeps ramp landings from preserving full air X-speed
    // when the ROM would halve/project it onto the slope.
    const int vector_angle =
        rom_vector_angle_from_speed(velocity_x, -velocity_y);
    const int angle = ground_angle & 0xFF;
    int ground_speed = std::abs(velocity_x);

    if (((angle + 0x10) & 0xFF) >= 0x20) {
        ground_speed = std::max(ground_speed, std::abs(velocity_y));

        const int vector_to_ground =
            sign_extend_8(angle - 0x40 - vector_angle);
        if (vector_to_ground == 0) {
            ground_speed = 0;
        } else if (vector_to_ground < 0x20 && vector_to_ground > -0x20) {
            ground_speed >>= 1;
        }
    }

    if (sign_extend_8(angle - 0x40 - vector_angle) >= 0) {
        ground_speed = -ground_speed;
    }
    if (facing_left) {
        ground_speed = -ground_speed;
    }
    return ground_speed;
}

void rom_plr_calc_xy_speed(Player& player) {
    int angle = player.ground_angle & 0xFF;
    if (player.facing_left) {
        angle = (angle + 0x80) & 0xFF;
    }

    const auto [velocity_x, rom_velocity_y] =
        rom_do_sine_lookup(angle, player.ground_speed);
    player.velocity_x = velocity_x;
    player.velocity_y = -rom_velocity_y;
}

int ground_velocity_y(const Player& player) {
    const int sine =
        static_cast<int>(std::sin(ground_angle_radians(player)) * kFixedOne);
    int velocity = -(sine * signed_ground_speed(player)) / kFixedOne;
    if ((player.ground_angle & 0xFF) == 0x29 && signed_ground_speed(player) < 0) {
        ++velocity;
    }
    if ((player.ground_angle & 0xFF) == 0x2D && signed_ground_speed(player) < 0) {
        ++velocity;
    }
    if ((player.ground_angle & 0xFF) == 0x13 && signed_ground_speed(player) < 0) {
        ++velocity;
    }
    if ((player.ground_angle & 0xFF) == 0x0A && signed_ground_speed(player) < 0) {
        ++velocity;
    }
    if (
        (player.ground_angle & 0x80) != 0 &&
        velocity > 0 &&
        player.ground_speed != kGroundMaxSpeed) {
        ++velocity;
    }
    return velocity;
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

int rom_sub_399cb1_slope_delta(const Player& player, int factor) {
    int angle = player.ground_angle & 0xFF;
    if (player.facing_left) {
        angle = (angle + 0x80) & 0xFF;
    }

    int sine = rom_do_sine_lookup(angle, kFixedOne).second;
    if (sine >= 0xB5) {
        sine += sine / 2;
    }
    return rom_mul_shift_8(sine, factor);
}

void rom_sub_399c88_apply_slope_force(Player& player) {
    const int angle_window = ((player.ground_angle & 0xFF) + 0x40) & 0xFF;
    if (angle_window > 0x80) {
        return;
    }

    player.ground_speed -= rom_sub_399cb1_slope_delta(player, 0x30);
    player.ground_speed = std::clamp(
        player.ground_speed, -kGroundSlopeMaxSpeed, kGroundSlopeMaxSpeed);
}

bool rom_sub_3994a2_check_skid(Player& player, int speed) {
    if (speed < 0x400 && speed > -0x300) {
        return false;
    }

    const int angle_window = ((player.ground_angle & 0xFF) + 0x20) & 0xFF;
    if (angle_window > 0x40) {
        return false;
    }

    if (player.skid_ticks == 0) {
        player.skid_ticks = kSkidAnimationTicks;
    }
    spawn_skid_dust(player);
    player.skid_dust_cooldown = 0;
    return true;
}

void rom_sub_399443_add_speed(
    Player& player, int speed, int delta, bool face_left_on_cross) {
    if (delta > 0 && speed >= kGroundMaxSpeed) {
        player.ground_speed = speed;
        return;
    }

    speed += delta;
    if (delta >= 0 && speed > kGroundMaxSpeed && speed - delta < kGroundMaxSpeed) {
        speed = kGroundMaxSpeed;
    }

    player.ground_speed = speed;
    if (delta < 0 && speed <= 0) {
        player.ground_speed = -speed;
        player.facing_left = face_left_on_cross;
    }
}

void rom_sub_399443_apply_friction(Player& player, int friction) {
    int delta = -friction;
    const int old_speed = player.ground_speed;
    if (old_speed < 0) {
        delta = -delta;
    }

    player.ground_speed += delta;
    if ((old_speed ^ player.ground_speed) < 0) {
        player.ground_speed = 0;
        return;
    }

    player.ground_speed = std::clamp(
        player.ground_speed, -0xF00, 0xF00);
}

void rom_sub_399443(Player& player, int movement) {
    constexpr int acceleration = kGroundAcceleration;
    constexpr int friction = kGroundFriction;
    constexpr int brake = -kGroundSkidDeceleration;

    if (movement == 0) {
        rom_sub_399443_apply_friction(player, friction);
        player.skid_dust_cooldown = 0;
        return;
    }

    player.walking_active = true;

    if (movement < 0) {
        int delta = acceleration;
        int speed = player.ground_speed;
        if (!player.facing_left) {
            delta = brake;
            if (speed <= 0) {
                player.facing_left = true;
                speed = -speed;
                delta = acceleration;
                rom_sub_399443_add_speed(
                    player, speed, delta, true);
                return;
            }
            if (rom_sub_3994a2_check_skid(player, speed)) {
                rom_sub_399443_add_speed(
                    player, speed, delta, true);
                return;
            }
        }

        if (speed < 0 && rom_sub_3994a2_check_skid(player, speed)) {
            speed = -speed;
            delta = brake;
            player.facing_left = false;
        }

        rom_sub_399443_add_speed(player, speed, delta, true);
        return;
    }

    int delta = acceleration;
    int speed = player.ground_speed;
    if (player.facing_left) {
        delta = brake;
        if (speed <= 0) {
            player.facing_left = false;
            speed = -speed;
            delta = acceleration;
            rom_sub_399443_add_speed(
                player, speed, delta, false);
            return;
        }
        if (rom_sub_3994a2_check_skid(player, speed)) {
            rom_sub_399443_add_speed(
                player, speed, delta, false);
            return;
        }
    }

    if (speed < 0 && rom_sub_3994a2_check_skid(player, speed)) {
        speed = -speed;
        delta = brake;
        player.facing_left = true;
    }

    rom_sub_399443_add_speed(player, speed, delta, false);
}

int signed_angle_to_ground_force(int angle) {
    if (angle == 0x29) {
        return -0x3C;
    }
    constexpr float pi = 3.14159265358979323846F;
    const float radians = static_cast<float>(angle) * pi / 128.0F;
    const int gravity =
        (angle & 0x80) != 0 ?
            kDownhillSlopeGravity :
            (angle == 0x40 ? 0x48 :
                (angle >= 0x10 && angle < 0x40 ? 0x2F : kSlopeGravity));
    return static_cast<int>(std::round(-std::sin(radians) * gravity));
}

int rom_y_to_view_y(int rom_y) {
    return kStageHeight - 1 - rom_y;
}

int view_y_to_rom_y(int view_y) {
    return kStageHeight - 1 - view_y;
}

int raw_view_y_to_rom_y(int y_raw) {
    return (((kStageHeight - 1) * kFixedOne) - y_raw) / kFixedOne;
}

std::optional<CollisionResponse> vertical_response_rom_y(
    const CollisionMask& collision, int x, int rom_y) {
    return collision.vertical_response(x, rom_y_to_view_y(rom_y));
}

std::optional<CollisionResponse> horizontal_response_rom_y(
    const CollisionMask& collision, int x, int rom_y) {
    return collision.horizontal_response(x, rom_y_to_view_y(rom_y));
}

RomHorizontalHit rom_bg_coll_chk1(
    const CollisionMask& collision, int x, int rom_y, int scan_length) {
    int offset = 0;
    int remaining = scan_length;
    int probe_x = x;
    while (remaining > 0) {
        const auto sample = horizontal_response_rom_y(collision, probe_x, rom_y);
        if (sample.has_value() && sample->response != 0x7F) {
            if (sample->response < 0) {
                return RomHorizontalHit{
                    true,
                    sample->response + offset,
                    sample->angle,
                    sample->response,
                    sample->collision_type,
                    sample->local_x,
                    sample->local_y,
                };
            }
        }
        --remaining;
        --probe_x;
        --offset;
    }
    return {};
}

RomHorizontalHit rom_bg_coll_chk2(
    const CollisionMask& collision, int x, int rom_y, int scan_length) {
    int offset = 0;
    int remaining = scan_length;
    int probe_x = x;
    while (remaining > 0) {
        const auto sample = horizontal_response_rom_y(collision, probe_x, rom_y);
        if (sample.has_value() && sample->response != 0x7F) {
            if (sample->response > 0) {
                return RomHorizontalHit{
                    true,
                    sample->response + offset,
                    sample->angle,
                    sample->response,
                    sample->collision_type,
                    sample->local_x,
                    sample->local_y,
                };
            }
        }
        --remaining;
        ++probe_x;
        ++offset;
    }
    return {};
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
                    sample->response,
                    sample->collision_type,
                    sample->local_x,
                    sample->local_y,
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
                    sample->response,
                    sample->collision_type,
                    sample->local_x,
                    sample->local_y,
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
    const CollisionMask& collision,
    int center_x,
    int rom_y,
    int scan_length,
    int radius_x = kSlopeProbeRadius) {
    const RomCollisionHit right =
        rom_bg_coll_chk4(collision, center_x + radius_x, rom_y, scan_length);
    const RomCollisionHit left =
        rom_bg_coll_chk4(collision, center_x - radius_x, rom_y, scan_length);
    if (!left.hit) {
        return right;
    }
    if (!right.hit) {
        return left;
    }
    return left.delta_y >= right.delta_y ? left : right;
}

RomCollisionHit rom_bg_coll_chk4_window(
    const CollisionMask& collision, int x, int rom_y, int min_offset, int max_offset) {
    RomCollisionHit best;
    for (int offset = min_offset; offset <= max_offset; ++offset) {
        const auto sample = vertical_response_rom_y(collision, x, rom_y + offset);
        if (!sample.has_value() || sample->response == 0x7F || sample->response <= 0) {
            continue;
        }

        const RomCollisionHit candidate{
            true,
            sample->response + offset,
            sample->angle,
            sample->response,
            sample->collision_type,
            sample->local_x,
            sample->local_y,
        };
        if (!best.hit || std::abs(candidate.delta_y) < std::abs(best.delta_y)) {
            best = candidate;
        }
    }
    return best;
}

RomCollisionHit choose_bg_coll_chk4_retain_pair(
    const CollisionMask& collision,
    int center_x,
    int rom_y,
    int scan_length,
    int current_angle = -1,
    int radius_x = kSlopeProbeRadius) {
    const RomCollisionHit right = rom_bg_coll_chk4_window(
        collision,
        center_x + radius_x,
        rom_y,
        -kGroundRetainSnapDown,
        scan_length - 1);
    const RomCollisionHit left = rom_bg_coll_chk4_window(
        collision,
        center_x - radius_x,
        rom_y,
        -kGroundRetainSnapDown,
        scan_length - 1);
    if (!left.hit) {
        return right;
    }
    if (!right.hit) {
        return left;
    }
    if (left.delta_y == right.delta_y && current_angle >= 0) {
        const int angle = current_angle & 0xFF;
        const bool left_matches = (left.angle & 0xFF) == angle;
        const bool right_matches = (right.angle & 0xFF) == angle;
        if (left_matches != right_matches) {
            return left_matches ? left : right;
        }
    }
    return left.delta_y >= right.delta_y ? left : right;
}

RomCollisionHit choose_bg_coll_chk3_pair(
    const CollisionMask& collision,
    int center_x,
    int rom_y,
    int scan_length,
    int radius_x = kSlopeProbeRadius) {
    const RomCollisionHit right =
        rom_bg_coll_chk3(collision, center_x + radius_x, rom_y, scan_length);
    const RomCollisionHit left =
        rom_bg_coll_chk3(collision, center_x - radius_x, rom_y, scan_length);
    if (!left.hit) {
        return right;
    }
    if (!right.hit) {
        return left;
    }
    return left.delta_y <= right.delta_y ? left : right;
}

RomHorizontalHit choose_bg_coll_chk1_pair(
    const CollisionMask& collision,
    int x,
    int rom_center_y,
    int scan_length,
    int radius_y = static_cast<int>(kPlayerHalfHeight)) {
    const RomHorizontalHit bottom =
        rom_bg_coll_chk1(
            collision,
            x,
            rom_center_y - radius_y,
            scan_length);
    const RomHorizontalHit top =
        rom_bg_coll_chk1(
            collision,
            x,
            rom_center_y + radius_y,
            scan_length);
    if (!bottom.hit) {
        return top;
    }
    if (!top.hit) {
        return bottom;
    }
    return top.delta_x <= bottom.delta_x ? top : bottom;
}

RomHorizontalHit choose_bg_coll_chk2_pair(
    const CollisionMask& collision,
    int x,
    int rom_center_y,
    int scan_length,
    int radius_y = static_cast<int>(kPlayerHalfHeight)) {
    const RomHorizontalHit bottom =
        rom_bg_coll_chk2(
            collision,
            x,
            rom_center_y - radius_y,
            scan_length);
    const RomHorizontalHit top =
        rom_bg_coll_chk2(
            collision,
            x,
            rom_center_y + radius_y,
            scan_length);
    if (!bottom.hit) {
        return top;
    }
    if (!top.hit) {
        return bottom;
    }
    return top.delta_x >= bottom.delta_x ? top : bottom;
}

int rom_angle_delta_abs(int next_angle, int current_angle) {
    int delta = (next_angle - current_angle) & 0xFF;
    if (delta >= 0x80) {
        delta -= 0x100;
    }
    return std::abs(delta);
}

int rom_collision_sector(const Player& player) {
    int angle = player.ground_angle & 0xFF;
    if (player.facing_left) {
        angle = (angle + 0x80) & 0xFF;
    }
    if (player.ground_speed < 0) {
        angle = (angle + 0x80) & 0xFF;
    }
    return (angle + 0x20) & 0xC0;
}

bool apply_rom_vertical_ground_pair(
    Player& player,
    const CollisionMask& collision,
    int scan_y_length,
    bool retain_ground) {
    const int debug_start_x = player.x_raw;
    const int debug_start_y = player.y_raw;
    const int rom_walk_ground_scan_length = std::clamp(
        std::max(9, scan_y_length + 8), 9, 0x20);
    const int center_x = player.x_raw / kFixedOne;
    const int rom_center_y = raw_view_y_to_rom_y(player.y_raw);
    const int radius_x = player.body_half_width;
    const int radius_y = player.body_half_height;
    const int rom_floor_probe_y = rom_center_y - radius_y;
    const int rom_ceiling_probe_y = rom_center_y + radius_y;
    player.debug_floor_probe_center_x = center_x;
    player.debug_floor_probe_rom_y = rom_floor_probe_y;

    RomCollisionHit floor_hit =
        retain_ground ?
            choose_bg_coll_chk4_retain_pair(
                collision,
                center_x,
                rom_floor_probe_y,
                rom_walk_ground_scan_length,
                player.ground_angle,
                radius_x) :
            choose_bg_coll_chk4_pair(
                collision, center_x, rom_floor_probe_y, rom_walk_ground_scan_length, radius_x);
    const RomCollisionHit ceiling_hit =
        choose_bg_coll_chk3_pair(
            collision, center_x, rom_ceiling_probe_y, rom_walk_ground_scan_length, radius_x);

    if (floor_hit.hit == ceiling_hit.hit) {
        if (!floor_hit.hit) {
            return false;
        }
    } else if (!floor_hit.hit) {
        floor_hit = ceiling_hit;
    }

    if (rom_angle_delta_abs(floor_hit.angle, player.ground_angle) >= 0x40) {
        return false;
    }

    const int previous_ground_angle = player.ground_angle & 0xFF;
    player.debug_floor_previous_angle = previous_ground_angle;
    player.debug_floor_velocity_x = player.velocity_x;
    player.debug_floor_velocity_y = player.velocity_y;
    player.debug_floor_hit_delta_y = floor_hit.delta_y;
    player.debug_floor_hit_response = floor_hit.response;
    player.debug_floor_hit_local_x = floor_hit.local_x;
    player.debug_floor_hit_local_y = floor_hit.local_y;
    player.debug_floor_hit_collision_type = floor_hit.collision_type;
    int ground_delta_y = floor_hit.delta_y;
    if (ground_delta_y > 0) {
        const int floor_angle = floor_hit.angle & 0xFF;
        if (
            floor_angle < 0xC0 ||
            player.velocity_y == 0 ||
            floor_hit.delta_y > 1 ||
            floor_hit.response >= 2) {
            --ground_delta_y;
        }
    } else if (ground_delta_y < 0 && player.velocity_y < 0) {
        const int uphill_bias =
            floor_hit.angle == 0 &&
                previous_ground_angle > 0 &&
                previous_ground_angle < 0x20 ? 0 :
            floor_hit.angle == 0 &&
                previous_ground_angle >= 0xC0 ? 0 :
            floor_hit.angle > 0 && floor_hit.angle < 0x10 ? 1 :
            floor_hit.angle >= 0x10 && floor_hit.angle < 0x40 ? 4 :
            (floor_hit.angle & 0xFF) >= 0xC0 ? 1 : 2;
        ground_delta_y = std::clamp(ground_delta_y + uphill_bias, -1, 1);
        if (
            floor_hit.angle > 0 &&
            floor_hit.angle < 0x10 &&
            floor_hit.delta_y == -1 &&
            floor_hit.response >= 4) {
            ground_delta_y = (floor_hit.response & 1) == 0 ? -1 : 0;
        }
        if (
            previous_ground_angle > 0 &&
            previous_ground_angle < 0x20 &&
            floor_hit.angle == 0 &&
            floor_hit.delta_y <= -3) {
            ground_delta_y = -2;
        }
    }
    if (
        ground_delta_y == 0 &&
        player.velocity_y < 0 &&
        previous_ground_angle != 0 &&
        floor_hit.angle == 0) {
        ground_delta_y = -1;
    }
    if (
        ground_delta_y == 0 &&
        floor_hit.delta_y == 0 &&
        player.velocity_y < 0 &&
        floor_hit.angle > 0 &&
        floor_hit.angle < 0x10) {
        ground_delta_y = 1;
    }
    if (
        ground_delta_y == 0 &&
        floor_hit.delta_y == 0 &&
        player.velocity_y > 0 &&
        floor_hit.angle > 0 &&
        floor_hit.angle < 0x10) {
        ground_delta_y = floor_hit.response >= 8 ? -1 : 1;
    }
    if (
        ground_delta_y < 0 &&
        player.velocity_y == 0 &&
        previous_ground_angle == 0 &&
        floor_hit.angle == 0) {
        ground_delta_y = 0;
    }
    if (
        ground_delta_y < 0 &&
        floor_hit.delta_y < 0 &&
        player.velocity_y >= 0 &&
        floor_hit.angle > 0 &&
        floor_hit.angle < 0x10) {
        ground_delta_y = floor_hit.delta_y <= -2 ? -1 : 0;
    }
    if (
        player.velocity_y == 0 &&
        previous_ground_angle == 0 &&
        (floor_hit.angle & 0xFF) >= 0xC0) {
        ground_delta_y = player.velocity_x < 0 ? 1 : 0;
    }
    if (
        ground_delta_y < -1 &&
        player.velocity_y > 0 &&
        (floor_hit.angle & 0xFF) >= 0xC0) {
        const int downhill_bias = floor_hit.response >= 3 ? 1 : 2;
        ground_delta_y = std::clamp(ground_delta_y + downhill_bias, -1, 1);
    }
    if (
        ground_delta_y == -1 &&
        player.velocity_y > 0 &&
        (floor_hit.angle & 0xFF) >= 0xC0 &&
        (floor_hit.response >= 4 || floor_hit.response <= 1)) {
        ground_delta_y = 0;
    }
    if (
        ground_delta_y < 0 &&
        player.velocity_y > 0 &&
        previous_ground_angle >= 0xC0 &&
        floor_hit.angle == 0) {
        ground_delta_y = 0;
    }
    if (
        previous_ground_angle == 0 &&
        player.velocity_y == 0 &&
        floor_hit.delta_y == 0 &&
        floor_hit.angle > 0 &&
        floor_hit.angle < 0x10 &&
        player.velocity_x > 0) {
        ground_delta_y = -1;
    }
    if (
        previous_ground_angle == 0 &&
        player.velocity_y == 0 &&
        floor_hit.delta_y == 1 &&
        floor_hit.angle > 0 &&
        floor_hit.angle < 0x10 &&
        player.velocity_x > 0) {
        ground_delta_y = 1;
    }
    if (
        previous_ground_angle == 0 &&
        player.velocity_y == 0 &&
        floor_hit.delta_y == 0 &&
        floor_hit.angle >= 0x10 &&
        floor_hit.angle < 0x40) {
        ground_delta_y = 4;
    }
    const int next_ground_angle = floor_hit.angle & 0xFF;
    if (
        previous_ground_angle > 0 &&
        previous_ground_angle < 0x40 &&
        next_ground_angle > previous_ground_angle &&
        next_ground_angle < 0x40 &&
        player.velocity_x > 0 &&
        player.velocity_y < 0) {
        const int transition_adjust =
            std::clamp((next_ground_angle - previous_ground_angle) / 8, 1, 3);
        player.x_raw -= transition_adjust * kFixedOne;
        ground_delta_y += transition_adjust;
    }
    if (
        previous_ground_angle > 0 &&
        previous_ground_angle < 0x20 &&
        floor_hit.angle == 0 &&
        player.velocity_y < 0 &&
        player.velocity_x < 0) {
        player.y_raw -= kFixedOne;
    }
    player.debug_floor_applied_delta_y = ground_delta_y;
    player.y_raw -= ground_delta_y * kFixedOne;
    player.air_time = 0.0F;
    player.ground_angle = floor_hit.angle;
    player.debug_walk_delta_x = player.x_raw - debug_start_x;
    player.debug_walk_delta_y = player.y_raw - debug_start_y;
    player.debug_walk_angle = floor_hit.angle & 0xFF;

    const int left_surface = rom_y_to_view_y(
        rom_floor_probe_y + rom_bg_coll_chk4(
            collision,
            center_x - radius_x,
            rom_floor_probe_y,
            rom_walk_ground_scan_length).delta_y);
    const int right_surface = rom_y_to_view_y(
        rom_floor_probe_y + rom_bg_coll_chk4(
            collision,
            center_x + radius_x,
            rom_floor_probe_y,
            rom_walk_ground_scan_length).delta_y);
    player.ground_slope =
        static_cast<float>(right_surface - left_surface) /
        static_cast<float>(radius_x * 2);
    return true;
}

void apply_rom_walk_collision(
    Player& player,
    const CollisionMask& collision,
    int scan_x_length,
    int scan_y_length,
    bool retain_ground = false) {
    const int sector = rom_collision_sector(player);
    if (sector == 0x40 || sector == 0xC0) {
        const int center_x = player.x_raw / kFixedOne;
        const int rom_center_y = raw_view_y_to_rom_y(player.y_raw);
        const int angle = player.ground_angle & 0xFF;
        const int radius_x = player.body_half_width;
        const int radius_y = player.body_half_height;
        int rom_y_offset = radius_y;
        if (sector == 0x40) {
            const int rotated = (angle - 0x40) & 0xFF;
            if (rotated > 0x80) {
                rom_y_offset = -rom_y_offset;
            }
        } else {
            const int rotated = (angle + 0x40) & 0xFF;
            if (rotated <= 0x80) {
                rom_y_offset = -rom_y_offset;
            }
        }
        const int probe_rom_y = rom_center_y + rom_y_offset;
        RomHorizontalHit side =
            rom_bg_coll_chk2(
                collision,
                center_x - radius_x,
                probe_rom_y,
                0x10);
        if (!side.hit) {
            side = rom_bg_coll_chk1(
                collision,
                center_x + radius_x,
                probe_rom_y,
                0x10);
        }
        if (side.hit && side.delta_x != 0) {
            if (
                player.velocity_x < 0 &&
                (player.ground_angle & 0xFF) >= 0x20 &&
                (player.ground_angle & 0xFF) < 0x40 &&
                side.angle > 0 &&
                side.angle < 0x20) {
                player.x_raw += side.delta_x * kFixedOne;
                player.x_raw -= 0x330;
                player.y_raw -= 2 * kFixedOne;
                player.ground_angle = side.angle;
                return;
            }
            if (
                player.velocity_x < 0 &&
                (player.ground_angle & 0xFF) >= 0x20 &&
                (player.ground_angle & 0xFF) < 0x40 &&
                side.angle >= 0x20 &&
                side.angle < 0x40) {
                player.x_raw += kFixedOne;
                player.ground_angle = side.angle;
                return;
            }
            if (
                side.angle == 0x40 &&
                player.velocity_x > 0 &&
                (player.ground_angle & 0xFF) > 0x20 &&
                (player.ground_angle & 0xFF) < 0x40) {
                player.x_raw += std::max(side.delta_x, -3) * kFixedOne;
                player.x_raw -= 0x36;
                player.ground_angle = side.angle;
                return;
            }
            player.x_raw += side.delta_x * kFixedOne;
            if (
                !((sector == 0x40 && side.angle == 0x40) ||
                  (sector == 0xC0 && side.angle == 0xC0))) {
                player.ground_angle = side.angle;
            }
            return;
        }

        if (sector == 0xC0) {
            const RomCollisionHit floor =
                choose_bg_coll_chk4_pair(collision, center_x, probe_rom_y, 0x10, radius_x);
            if (floor.hit && floor.delta_y != 0) {
                player.y_raw -= floor.delta_y * kFixedOne;
                player.ground_angle = floor.angle;
            }
        } else {
            const RomCollisionHit ceiling =
                choose_bg_coll_chk3_pair(collision, center_x, probe_rom_y, 0x10, radius_x);
            if (ceiling.hit && ceiling.delta_y != 0) {
                player.y_raw -= ceiling.delta_y * kFixedOne;
                if ((ceiling.angle & 0xFF) != 0x80) {
                    player.ground_angle = ceiling.angle;
                } else {
                    player.ground_speed = 0;
                    player.walking_active = false;
                }
            }
        }
        return;
    }

    if (sector == 0x00 || sector == 0x80) {
        if (apply_rom_vertical_ground_pair(player, collision, scan_y_length, retain_ground)) {
            if ((player.ground_angle & 0xFF) == 0 && player.velocity_x < 0) {
                const int center_x = player.x_raw / kFixedOne;
                const int rom_center_y = raw_view_y_to_rom_y(player.y_raw);
                const int radius_x = player.body_half_width;
                const int radius_y = player.body_half_height;
                const RomHorizontalHit side =
                    choose_bg_coll_chk2_pair(
                        collision,
                        center_x - radius_x,
                        rom_center_y,
                        scan_x_length,
                        radius_y);
                if (side.hit && side.delta_x != 0) {
                    player.x_raw += side.delta_x * kFixedOne;
                    player.ground_speed = 0;
                    player.velocity_x = 0;
                    player.walking_active = false;
                }
            } else if ((player.ground_angle & 0xFF) == 0 && player.velocity_x > 0) {
                const int center_x = player.x_raw / kFixedOne;
                const int rom_center_y = raw_view_y_to_rom_y(player.y_raw);
                const int radius_x = player.body_half_width;
                const int radius_y = player.body_half_height;
                const RomHorizontalHit side =
                    choose_bg_coll_chk1_pair(
                        collision,
                        center_x + radius_x,
                        rom_center_y,
                        scan_x_length,
                        radius_y);
                if (side.hit && side.delta_x != 0) {
                    player.x_raw += side.delta_x * kFixedOne;
                    player.ground_speed = 0;
                    player.velocity_x = 0;
                    player.walking_active = false;
                }
            }
            return;
        }

        const int center_x = player.x_raw / kFixedOne;
        const int rom_center_y = raw_view_y_to_rom_y(player.y_raw);
        const int radius_x = player.body_half_width;
        const int radius_y = player.body_half_height;
        int side_offset = radius_x;
        if (sector == 0x00) {
            if ((player.ground_angle & 0xFF) > 0x80) {
                side_offset = -side_offset;
            }
            const RomHorizontalHit side =
                choose_bg_coll_chk1_pair(
                    collision, center_x + side_offset, rom_center_y, scan_x_length, radius_y);
            if (side.hit && side.delta_x != 0) {
                player.x_raw += side.delta_x * kFixedOne;
                player.velocity_x = 0;
                if (side.angle != 0x40) {
                    player.ground_angle = side.angle;
                }
            }
        } else {
            const int signed_angle =
                (player.ground_angle & 0x80) != 0 ?
                    (player.ground_angle - 0x100) :
                    player.ground_angle;
            if (player.ground_angle == 0 || signed_angle < 0) {
                side_offset = -side_offset;
            }
            const RomHorizontalHit side =
                choose_bg_coll_chk2_pair(
                    collision, center_x + side_offset, rom_center_y, scan_x_length, radius_y);
            if (side.hit && side.delta_x != 0) {
                player.x_raw += side.delta_x * kFixedOne;
                player.velocity_x = 0;
                if (side.angle != -0x40) {
                    player.ground_angle = side.angle;
                }
            }
        }
    }
}

bool update_ground_contact(Player& player, const CollisionMask& collision, bool retain_ground = false) {
    // Plr_IsOnGround -> CalcPlrMoveSpdY stores byte_668F as the integer
    // movement delta plus one, then sub_39BC22 uses that as BGCollChk4's scan
    // length.  A fixed 9px scan can overshoot the intended landing cell and
    // select the next slope angle one tile too early.
    const int rom_ground_scan_length =
        retain_ground ?
            9 :
            std::clamp(
                std::abs((player.y_raw / kFixedOne) -
                         (player.previous_y_raw / kFixedOne)) + 1,
                1,
                0x20);
    const int center_x = player.x_raw / kFixedOne;
    const int rom_center_y = raw_view_y_to_rom_y(player.y_raw);
    const int radius_x = player.body_half_width;
    const int radius_y = player.body_half_height;
    const int rom_floor_probe_y = rom_center_y - radius_y;
    RomCollisionHit floor_hit =
        choose_bg_coll_chk4_pair(
            collision, center_x, rom_floor_probe_y, rom_ground_scan_length, radius_x);
    if (!floor_hit.hit && retain_ground) {
        floor_hit = choose_bg_coll_chk4_retain_pair(
            collision, center_x, rom_floor_probe_y, rom_ground_scan_length, -1, radius_x);
    }
    if (!floor_hit.hit && !retain_ground && player.velocity_y >= kGroundMaxSpeed) {
        floor_hit = rom_bg_coll_chk4_window(
            collision,
            center_x,
            rom_floor_probe_y,
            -kGroundRetainSnapDown,
            rom_ground_scan_length - 1);
    }
    if (!floor_hit.hit) {
        return false;
    }

    player.y_raw -= floor_hit.delta_y * kFixedOne;
    player.velocity_y = 0;
    player.air_time = 0.0F;
    player.ground_angle = floor_hit.angle;
    return true;
}

void prime_player_ground_contact(Player& player, const CollisionMask& collision) {
    (void)update_ground_contact(player, collision, true);
    player.grounded = true;
    player.leave_ground_rotation_delay = 0;
    player.ground_speed = 0;
    player.velocity_x = 0;
    player.velocity_y = 0;
    player.air_time = 0.0F;
}

bool rom_check_no_ground(Player& player, const CollisionMask& collision) {
    const int debug_start_x = player.x_raw;
    const int debug_start_y = player.y_raw;
    const int sector = (player.ground_angle + 0x20) & 0xFF;
    player.debug_no_ground_sector = sector;
    player.debug_no_ground_delta_x = 0;
    player.debug_no_ground_delta_y = 0;
    player.debug_no_ground_angle = player.ground_angle & 0xFF;
    player.debug_no_ground_hit = false;
    player.debug_no_ground_probe_a_delta = 0;
    player.debug_no_ground_probe_a_angle = player.ground_angle & 0xFF;
    player.debug_no_ground_probe_a_hit = false;
    player.debug_no_ground_probe_b_delta = 0;
    player.debug_no_ground_probe_b_angle = player.ground_angle & 0xFF;
    player.debug_no_ground_probe_b_hit = false;
    player.debug_no_ground_probe_a_delta = 0;
    player.debug_no_ground_probe_a_angle = 0;
    player.debug_no_ground_probe_a_hit = false;
    player.debug_no_ground_probe_b_delta = 0;
    player.debug_no_ground_probe_b_angle = 0;
    player.debug_no_ground_probe_b_hit = false;
    const int center_x = player.x_raw / kFixedOne;
    const int rom_center_y = raw_view_y_to_rom_y(player.y_raw);
    const int radius_x = player.body_half_width;
    const int radius_y = player.body_half_height;
    constexpr int kRomNoGroundScanLength = 8;

    if (sector > 0x40 && sector <= 0x80) {
        const int x = center_x + radius_x + 8;
        const RomHorizontalHit top = rom_bg_coll_chk1(
            collision, x, rom_center_y + radius_y, 0x10);
        const RomHorizontalHit bottom = rom_bg_coll_chk1(
            collision, x, rom_center_y - radius_y, 0x10);
        player.debug_no_ground_probe_a_hit = top.hit;
        player.debug_no_ground_probe_a_delta = top.delta_x;
        player.debug_no_ground_probe_a_angle = top.angle & 0xFF;
        player.debug_no_ground_probe_b_hit = bottom.hit;
        player.debug_no_ground_probe_b_delta = bottom.delta_x;
        player.debug_no_ground_probe_b_angle = bottom.angle & 0xFF;
        RomHorizontalHit selected;
        if (!top.hit) {
            selected = bottom;
        } else if (!bottom.hit) {
            selected = top;
        } else {
            selected = top.delta_x <= bottom.delta_x ? top : bottom;
        }
        if (!selected.hit || selected.delta_x == 0) {
            return false;
        }
        player.x_raw += (selected.delta_x + 8) * kFixedOne;
        player.ground_angle = selected.angle;
        player.debug_no_ground_delta_x = player.x_raw - debug_start_x;
        player.debug_no_ground_delta_y = player.y_raw - debug_start_y;
        player.debug_no_ground_angle = selected.angle & 0xFF;
        player.debug_no_ground_hit = true;
        return true;
    }

    if (sector > 0x80 && sector <= 0xC0) {
        const int rom_ceiling_probe_y =
            rom_center_y + radius_y + 8;
        const RomCollisionHit right = rom_bg_coll_chk3(
            collision,
            center_x + radius_x,
            rom_ceiling_probe_y,
            kRomNoGroundScanLength);
        const RomCollisionHit left = rom_bg_coll_chk3(
            collision,
            center_x - radius_x,
            rom_ceiling_probe_y,
            kRomNoGroundScanLength);
        RomCollisionHit selected;
        if (!right.hit) {
            selected = left;
        } else if (!left.hit) {
            selected = right;
        } else {
            selected = right.delta_y <= left.delta_y ? right : left;
        }
        if (!selected.hit || selected.delta_y == 0) {
            return false;
        }
        player.y_raw -= (selected.delta_y + 8) * kFixedOne;
        player.ground_angle = selected.angle;
        player.debug_no_ground_delta_x = player.x_raw - debug_start_x;
        player.debug_no_ground_delta_y = player.y_raw - debug_start_y;
        player.debug_no_ground_angle = selected.angle & 0xFF;
        player.debug_no_ground_hit = true;
        return true;
    }

    if (sector > 0xC0) {
        const int x = center_x - radius_x - 8;
        const RomHorizontalHit top = rom_bg_coll_chk2(
            collision, x, rom_center_y + radius_y, 0x10);
        const RomHorizontalHit bottom = rom_bg_coll_chk2(
            collision, x, rom_center_y - radius_y, 0x10);
        player.debug_no_ground_probe_a_hit = top.hit;
        player.debug_no_ground_probe_a_delta = top.delta_x;
        player.debug_no_ground_probe_a_angle = top.angle & 0xFF;
        player.debug_no_ground_probe_b_hit = bottom.hit;
        player.debug_no_ground_probe_b_delta = bottom.delta_x;
        player.debug_no_ground_probe_b_angle = bottom.angle & 0xFF;
        RomHorizontalHit selected;
        if (!top.hit) {
            selected = bottom;
        } else if (!bottom.hit) {
            selected = top;
        } else {
            selected = top.delta_x >= bottom.delta_x ? top : bottom;
        }
        if (!selected.hit || selected.delta_x == 0) {
            return false;
        }
        player.x_raw += (selected.delta_x - 8) * kFixedOne;
        player.ground_angle = selected.angle;
        player.debug_no_ground_delta_x = player.x_raw - debug_start_x;
        player.debug_no_ground_delta_y = player.y_raw - debug_start_y;
        player.debug_no_ground_angle = selected.angle & 0xFF;
        player.debug_no_ground_hit = true;
        return true;
    }

    const int rom_floor_probe_y =
        rom_center_y - radius_y - 8;
    const RomCollisionHit right = rom_bg_coll_chk4(
        collision,
        center_x + radius_x,
        rom_floor_probe_y,
        kRomNoGroundScanLength);
    const RomCollisionHit left = rom_bg_coll_chk4(
        collision,
        center_x - radius_x,
        rom_floor_probe_y,
        kRomNoGroundScanLength);

    RomCollisionHit selected;
    if (!right.hit) {
        selected = left;
    } else if (!left.hit) {
        selected = right;
    } else if (left.delta_y > right.delta_y) {
        selected = left;
    } else if (left.delta_y != right.delta_y) {
        selected = right;
    } else if (left.angle == player.ground_angle) {
        selected = left;
    } else {
        selected = right;
    }

    if (!selected.hit || selected.delta_y == 0) {
        selected = rom_bg_coll_chk4(
            collision,
            center_x,
            rom_floor_probe_y,
            kRomNoGroundScanLength);
        if (!selected.hit || selected.delta_y == 0) {
            return false;
        }
    }

    const int rom_delta_y = selected.delta_y - 8;
    player.y_raw -= rom_delta_y * kFixedOne;
    player.ground_angle = selected.angle;
    player.air_time = 0.0F;
    player.debug_no_ground_delta_x = player.x_raw - debug_start_x;
    player.debug_no_ground_delta_y = player.y_raw - debug_start_y;
    player.debug_no_ground_angle = selected.angle & 0xFF;
    player.debug_no_ground_hit = true;
    return true;
}

bool rom_has_ground_support(const Player& player, const CollisionMask& collision) {
    const int sector = (player.ground_angle + 0x20) & 0xFF;
    const int center_x = player.x_raw / kFixedOne;
    const int rom_center_y = raw_view_y_to_rom_y(player.y_raw);
    const int radius_x = player.body_half_width;
    const int radius_y = player.body_half_height;
    constexpr int kRomNoGroundScanLength = 8;

    if (sector > 0x40 && sector <= 0x80) {
        const int x = center_x + radius_x + 8;
        const RomHorizontalHit top = rom_bg_coll_chk1(
            collision, x, rom_center_y + radius_y, 0x10);
        const RomHorizontalHit bottom = rom_bg_coll_chk1(
            collision, x, rom_center_y - radius_y, 0x10);
        return (top.hit && top.delta_x != 0) || (bottom.hit && bottom.delta_x != 0);
    }

    if (sector > 0x80 && sector <= 0xC0) {
        const int rom_ceiling_probe_y =
            rom_center_y + radius_y + 8;
        const RomCollisionHit right = rom_bg_coll_chk3(
            collision,
            center_x + radius_x,
            rom_ceiling_probe_y,
            kRomNoGroundScanLength);
        const RomCollisionHit left = rom_bg_coll_chk3(
            collision,
            center_x - radius_x,
            rom_ceiling_probe_y,
            kRomNoGroundScanLength);
        return (right.hit && right.delta_y != 0) || (left.hit && left.delta_y != 0);
    }

    if (sector > 0xC0) {
        const int x = center_x - radius_x - 8;
        const RomHorizontalHit top = rom_bg_coll_chk2(
            collision, x, rom_center_y + radius_y, 0x10);
        const RomHorizontalHit bottom = rom_bg_coll_chk2(
            collision, x, rom_center_y - radius_y, 0x10);
        return (top.hit && top.delta_x != 0) || (bottom.hit && bottom.delta_x != 0);
    }

    const int rom_floor_probe_y =
        rom_center_y - radius_y - 8;
    const RomCollisionHit right = rom_bg_coll_chk4(
        collision,
        center_x + radius_x,
        rom_floor_probe_y,
        kRomNoGroundScanLength);
    const RomCollisionHit left = rom_bg_coll_chk4(
        collision,
        center_x - radius_x,
        rom_floor_probe_y,
        kRomNoGroundScanLength);
    return (right.hit && right.delta_y != 0) || (left.hit && left.delta_y != 0);
}

void write_collision_debug_row(
    std::ostream& output,
    int frame,
    const Player& player,
    const CollisionMask& collision,
    int movement,
    bool jump_pressed,
    bool jump_held,
    bool was_grounded) {
    const int center_x = player.x_raw / kFixedOne;
    const int rom_center_y = raw_view_y_to_rom_y(player.y_raw);
    const int radius_x = player.body_half_width;
    const int radius_y = player.body_half_height;
    const int scan_x_length =
        std::clamp(
            std::abs((player.x_raw / kFixedOne) -
                     (player.previous_x_raw / kFixedOne)) + 1,
            1,
            0x20);
    const int scan_y_length =
        std::clamp(
            std::abs((player.y_raw / kFixedOne) -
                     (player.previous_y_raw / kFixedOne)) + 1,
            1,
            0x20);
    constexpr int kRomNoGroundScanLength = 8;
    const int rom_floor_probe_y = rom_center_y - radius_y - 8;
    const RomCollisionHit floor_right = rom_bg_coll_chk4(
        collision,
        center_x + radius_x,
        rom_floor_probe_y,
        kRomNoGroundScanLength);
    const RomCollisionHit floor_left = rom_bg_coll_chk4(
        collision,
        center_x - radius_x,
        rom_floor_probe_y,
        kRomNoGroundScanLength);
    const bool support = rom_has_ground_support(player, collision);
    Player no_ground_probe = player;
    const bool no_ground_recovered = rom_check_no_ground(no_ground_probe, collision);
    Player landing_probe = player;
    const bool landing_contact = update_ground_contact(landing_probe, collision);

    output
        << frame << ','
        << movement << ','
        << (jump_pressed ? 1 : 0) << ','
        << (jump_held ? 1 : 0) << ','
        << (was_grounded ? 1 : 0) << ','
        << (player.grounded ? 1 : 0) << ','
        << ((was_grounded && !player.grounded) ? 1 : 0) << ','
        << player.x_raw << ','
        << player.y_raw << ','
        << center_x << ','
        << rom_center_y << ','
        << player.previous_x_raw << ','
        << player.previous_y_raw << ','
        << player.ground_speed << ','
        << player.velocity_x << ','
        << player.velocity_y << ','
        << (player.ground_angle & 0xFF) << ','
        << radius_x << ','
        << radius_y << ','
        << scan_x_length << ','
        << scan_y_length << ','
        << rom_floor_probe_y << ','
        << (support ? 1 : 0) << ','
        << (no_ground_recovered ? 1 : 0) << ','
        << (landing_contact ? 1 : 0) << ','
        << (floor_left.hit ? 1 : 0) << ','
        << floor_left.delta_y << ','
        << floor_left.angle << ','
        << floor_left.response << ','
        << floor_left.collision_type << ','
        << floor_left.local_x << ','
        << floor_left.local_y << ','
        << (floor_right.hit ? 1 : 0) << ','
        << floor_right.delta_y << ','
        << floor_right.angle << ','
        << floor_right.response << ','
        << floor_right.collision_type << ','
        << floor_right.local_x << ','
        << floor_right.local_y << ','
        << no_ground_probe.x_raw << ','
        << no_ground_probe.y_raw << ','
        << (no_ground_probe.ground_angle & 0xFF) << ','
        << landing_probe.x_raw << ','
        << landing_probe.y_raw << ','
        << (landing_probe.ground_angle & 0xFF) << '\n';
}

bool rom_should_leave_ground_at_low_speed(const Player& player) {
    const int angle_window = ((player.ground_angle & 0xFF) - 0x40) & 0xFF;
    if (angle_window > 0x80) {
        return false;
    }
    return ground_speed_magnitude(player) < 0x100;
}

void rom_rotate_player_angle_to_zero(Player& player) {
    int angle = player.ground_angle & 0xFF;
    if (angle == 0) {
        return;
    }

    int signed_angle = angle;
    if (signed_angle >= 0x80) {
        signed_angle -= 0x100;
    }

    if (signed_angle > 0) {
        signed_angle = std::max(0, signed_angle - 4);
    } else {
        signed_angle = std::min(0, signed_angle + 4);
    }
    player.ground_angle = signed_angle & 0xFF;
}

void rom_leave_ground(Player& player) {
    player.grounded = false;
    player.jump_release_limited = false;
    player.walking_active = false;
    player.pending_standing_hbox = false;
    player.leave_ground_rotation_delay = 1;
}

void rom_enter_standing_hbox(Player& player) {
    player.body_half_width = 7;
    if (player.body_half_height != 13) {
        player.body_half_height = 13;
        player.y_raw -= 3 * kFixedOne;
    }
}

void rom_plr_air_drag(Player& player, int movement) {
    // Port of PlrAirDrag.  Air control approaches +/-0x800, but if slope or
    // ground momentum already pushed X-speed past that cap the ROM preserves
    // that over-cap speed instead of clamping it down in one frame.
    const int max_air_speed = kAirMaxXSpeed;
    const int accel = kAirAcceleration;

    if (movement > 0) {
        player.facing_left = false;
        int next_velocity = player.velocity_x + accel;
        if (next_velocity > max_air_speed) {
            const int previous_velocity = next_velocity - accel;
            next_velocity =
                previous_velocity > max_air_speed ? previous_velocity : max_air_speed;
        }
        player.velocity_x = next_velocity;
    } else if (movement < 0) {
        player.facing_left = true;
        int next_velocity = player.velocity_x - accel;
        const int min_air_speed = -max_air_speed;
        if (next_velocity < min_air_speed) {
            const int previous_velocity = next_velocity + accel;
            next_velocity =
                previous_velocity < min_air_speed ? previous_velocity : min_air_speed;
        }
        player.velocity_x = next_velocity;
    }

    // PlrAirDrag's passive horizontal air drag only runs once the ROM vertical
    // speed drops below +0x400.  The viewer stores upward motion as negative,
    // so fast upward motion is <= -0x400 here and must return unchanged.
    if (player.velocity_y <= -kJumpReleaseLimit) {
        return;
    }

    const int drag = player.velocity_x >> 5;
    if (drag == 0) {
        return;
    }
    const int next_velocity = player.velocity_x - drag;
    if ((player.velocity_x > 0 && next_velocity < 0) ||
        (player.velocity_x < 0 && next_velocity > 0)) {
        player.velocity_x = 0;
    } else {
        player.velocity_x = next_velocity;
    }
}

void update_player(Player& player, const CollisionMask& collision,
                   int movement, bool jump_pressed, bool jump_held,
                   bool input_up, bool input_down) {
    player.debug_walk_delta_x = 0;
    player.debug_walk_delta_y = 0;
    player.debug_walk_angle = player.ground_angle & 0xFF;
    player.debug_no_ground_sector = 0;
    player.debug_no_ground_delta_x = 0;
    player.debug_no_ground_delta_y = 0;
    player.debug_no_ground_angle = player.ground_angle & 0xFF;
    player.debug_no_ground_hit = false;
    player.debug_floor_probe_center_x = 0;
    player.debug_floor_probe_rom_y = 0;
    player.debug_floor_applied_delta_y = 0;
    player.debug_floor_previous_angle = player.ground_angle & 0xFF;
    player.debug_floor_velocity_x = player.velocity_x;
    player.debug_floor_velocity_y = player.velocity_y;

    player.movement_input = movement;
    player.jump_held = jump_held;
    player.input_up = input_up;
    player.input_down = input_down;

    bool started_jump_this_tick = false;
    if (player.pending_jump && player.grounded) {
        player.pending_jump = false;
        player.pending_standing_hbox = false;
        const auto [jump_impulse_x, rom_jump_impulse_y] =
            rom_do_sine_lookup((player.ground_angle + 0x40) & 0xFF, kJumpImpulse);
        player.velocity_x += jump_impulse_x;
        player.velocity_y -= rom_jump_impulse_y;
        if (player.body_half_height != 10) {
            player.body_half_height = 10;
            player.y_raw += 3 * kFixedOne;
        }
        player.grounded = false;
        player.leave_ground_rotation_delay = 0;
        started_jump_this_tick = true;
        player.jump_release_limited = true;
        player.walking_active = false;
        player.air_time = 0.0F;
    } else {
        player.pending_jump = false;
    }

    if (
        player.grounded &&
        (player.ground_angle & 0xFF) == 0x40 &&
        movement == 0 &&
        ground_speed_magnitude(player) <= 0x778) {
        player.velocity_x = 0;
        player.velocity_y = -ground_speed_magnitude(player);
        player.grounded = false;
        player.leave_ground_rotation_delay = 1;
        player.jump_release_limited = false;
        player.walking_active = false;
    }

    if (player.grounded) {
        if (player.pending_standing_hbox) {
            rom_enter_standing_hbox(player);
            player.pending_standing_hbox = false;
        }
        rom_sub_399c88_apply_slope_force(player);
        rom_sub_399443(player, movement);
        if (ground_speed_magnitude(player) < kGroundStopThreshold &&
            movement == 0) {
            player.ground_speed = 0;
            player.walking_active = false;
        }
        rom_plr_calc_xy_speed(player);
        if (player.skid_ticks > 0) {
            --player.skid_ticks;
        }
        if (player.skid_dust_cooldown > 0) {
            --player.skid_dust_cooldown;
        }
    } else if (!started_jump_this_tick) {
        player.skid_dust_cooldown = 0;
        rom_plr_air_drag(player, movement);
    }

    if (
        !player.grounded &&
        player.jump_release_limited &&
        !jump_held &&
        player.velocity_y < -kJumpReleaseLimit) {
        player.velocity_y = -kJumpReleaseLimit;
    }

    if (!player.grounded) {
        player.velocity_y = std::min(player.velocity_y + kGravity, kFallMaxSpeed);
    }

    player.previous_x_raw = player.x_raw;
    player.previous_y_raw = player.y_raw;

    player.x_raw += player.velocity_x;
    if (player.x_raw < kRomPlayerLeftBoundaryRaw) {
        player.x_raw = kRomPlayerLeftBoundaryRaw;
        if (player.velocity_x < 0) {
            player.velocity_x = 0;
        }
        if (player.ground_speed > 0 && player.facing_left) {
            player.ground_speed = 0;
            player.walking_active = false;
        }
    }
    player.x_raw = std::min(
        player.x_raw,
        static_cast<int>((kCameraMaxX + kLogicalWidth - kPlayerHalfWidth) * kFixedOne));

    player.y_raw += player.velocity_y;

    if (player.grounded) {
        const int projected_velocity_x = player.velocity_x;
        const int projected_velocity_y = player.velocity_y;
        if (projected_velocity_x == 0 && projected_velocity_y == 0) {
            // The ROM does not re-snap Y on the exact tick movement stops;
            // doing so created the visible one-pixel walk->idle pop.
            player.grounded = true;
        } else {
        const int start_ground_angle = player.ground_angle & 0xFF;
        const int scan_x_length =
            std::clamp(
                std::abs((player.x_raw / kFixedOne) -
                         (player.previous_x_raw / kFixedOne)) + 1,
                1,
                0x20);
        const int scan_y_length =
            std::clamp(
                std::abs((player.y_raw / kFixedOne) -
                         (player.previous_y_raw / kFixedOne)) + 1,
                1,
                0x20);
        const int moved_x_raw = player.x_raw;
        apply_rom_walk_collision(
            player, collision, scan_x_length, scan_y_length, true);
        if (
            start_ground_angle == 0x2D &&
            (player.ground_angle & 0xFF) == 0x2D &&
            projected_velocity_x < 0 &&
            projected_velocity_y > 0 &&
            player.x_raw == moved_x_raw) {
            player.x_raw += kFixedOne;
        }
        // Plr_CheckNoGrnd/sub_39B83E is an apply-correction routine, not just
        // a support predicate.  The current sub_39B508 port is still partial,
        // though, and can already apply the same floor correction one frame
        // early on shallow slopes.  Until sub_39B508 is fully line-ported, keep
        // the support gate for floor sectors and use sub_39B83E when support is
        // genuinely missing.  Wall sectors still use explicit support probing
        // to avoid false left-ground transitions at the 0x40/0xC0 boundaries.
        const int support_sector = (player.ground_angle + 0x20) & 0xFF;
        const bool wall_sector =
            (support_sector >= 0x40 && support_sector <= 0x80) ||
            (support_sector > 0xC0);
        const int start_support_sector = (start_ground_angle + 0x20) & 0xFF;
        const bool started_wall_sector =
            (start_support_sector >= 0x40 && start_support_sector <= 0x80) ||
            (start_support_sector > 0xC0);
        const bool entered_wall_sector = wall_sector && !started_wall_sector;
        bool kept_ground = false;
        if (wall_sector) {
            if (entered_wall_sector && player.debug_walk_delta_x != 0) {
                kept_ground = true;
            } else {
                kept_ground = rom_has_ground_support(player, collision);
            }
        } else {
            kept_ground =
                rom_has_ground_support(player, collision) ||
                rom_check_no_ground(player, collision);
        }
        if (!kept_ground || rom_should_leave_ground_at_low_speed(player)) {
            rom_leave_ground(player);
        } else {
            player.grounded = true;
        }
        }
    } else if (player.velocity_y >= 0) {
        const int landing_velocity_x = player.velocity_x;
        const int landing_velocity_y = player.velocity_y;
        if (update_ground_contact(player, collision)) {
            player.grounded = true;
            player.leave_ground_rotation_delay = 0;
            player.jump_release_limited = false;

            const int landing_angle = player.ground_angle & 0xFF;
            if (landing_angle == 0x2D && landing_velocity_x == 0) {
                player.y_raw -= kFixedOne;
                player.ground_angle = 0x29;
                player.ground_speed = -std::abs(landing_velocity_y);
                player.velocity_y = landing_velocity_y;
            } else {
                player.ground_speed = rom_sub_39abec_landing_speed(
                    landing_velocity_x,
                    landing_velocity_y,
                    player.ground_angle,
                    player.facing_left);
                player.velocity_y = landing_velocity_y;
            }
            player.walking_active = player.ground_speed != 0;
            player.pending_standing_hbox = true;
        }
    }

    if (jump_pressed && player.grounded) {
        player.pending_jump = true;
    }

    if (!player.grounded) {
        player.ground_slope = 0.0F;
        if (player.leave_ground_rotation_delay > 0) {
            --player.leave_ground_rotation_delay;
        } else {
            rom_rotate_player_angle_to_zero(player);
        }
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

    // The player position is fixed-point, but the ROM sprite object lands on
    // an integer screen Y.  Flat ground reached from slopes can sit in the low
    // half of a fixed-point pixel; if we simply floor that, the hilltop-flat
    // sprite appears one pixel high.  Do not apply that bias to exact integer
    // flats though: the intro flat is already aligned and becomes one pixel
    // buried if it gets the slope-exit bias too.
    const int player_y_subpixel = player.y_raw & (kFixedOne - 1);
    const float flat_subpixel_draw_bias =
        player.grounded &&
        (player.ground_angle & 0xFF) == 0 &&
        player_y_subpixel > 0 &&
        player_y_subpixel < (kFixedOne / 2) ?
            1.0F :
            0.0F;
    const float player_screen_y =
        std::floor(player.y() - camera_y) + flat_subpixel_draw_bias;
    SDL_FRect sonic_destination{
        std::floor(player.x() - camera_x - sonic.origin_x),
        player_screen_y - sonic.origin_y,
        sonic.width,
        sonic.height,
    };
    if (!SDL_RenderTextureRotated(
            app.renderer,
            sonic.texture.value,
            nullptr,
            &sonic_destination,
            player_render_angle_degrees(player),
            nullptr,
            player.facing_left ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE)) {
        return fail("Unable to render Sonic");
    }

    if (show_collision) {
        const float marker_x = std::floor(player.x() - camera_x);
        const float collision_foot_y =
            player_screen_y + static_cast<float>(player.body_half_height);
        const float sprite_foot_y =
            std::floor(player_screen_y - sonic.origin_y + sonic.opaque_bottom_y);
        SDL_SetRenderDrawColor(app.renderer, 0, 255, 0, 255);
        SDL_FRect collision_marker{marker_x - 2.0F, collision_foot_y, 5.0F, 1.0F};
        SDL_RenderFillRect(app.renderer, &collision_marker);
        SDL_SetRenderDrawColor(app.renderer, 255, 255, 0, 255);
        SDL_FRect sprite_marker{marker_x - 2.0F, sprite_foot_y, 5.0F, 1.0F};
        SDL_RenderFillRect(app.renderer, &sprite_marker);
    }

    SDL_RenderPresent(app.renderer);
    return true;
}

bool render_title_frame(
    Application& app,
    SDL_Texture* plane2,
    SDL_Texture* plane1,
    SDL_Texture* sonic,
    SDL_Texture* prompt,
    bool prompt_visible) {
    SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, 255);
    SDL_RenderClear(app.renderer);
    if (plane2 != nullptr && !SDL_RenderTexture(app.renderer, plane2, nullptr, nullptr)) {
        return fail("Unable to render title screen");
    }
    if (plane1 != nullptr && !SDL_RenderTexture(app.renderer, plane1, nullptr, nullptr)) {
        return fail("Unable to render title foreground");
    }
    if (sonic != nullptr && !SDL_RenderTexture(app.renderer, sonic, nullptr, nullptr)) {
        return fail("Unable to render title Sonic");
    }
    if (prompt != nullptr && prompt_visible) {
        if (!SDL_RenderTexture(app.renderer, prompt, nullptr, nullptr)) {
            return fail("Unable to render title prompt");
        }
    }
    SDL_RenderPresent(app.renderer);
    return true;
}

bool render_title_intro_frame(Application& app, SDL_Texture* frame_texture) {
    SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, 255);
    SDL_RenderClear(app.renderer);
    if (frame_texture != nullptr &&
        !SDL_RenderTexture(app.renderer, frame_texture, nullptr, nullptr)) {
        return fail("Unable to render title intro frame");
    }
    SDL_RenderPresent(app.renderer);
    return true;
}

int run_title_screen(
    Application& app,
    const std::filesystem::path& title_directory,
    bool smoke_test) {
    const auto plane2_path = title_directory / "plane2.png";
    const auto plane1_path = title_directory / "plane1.png";
    const auto press_a_off_path = title_directory / "press_a_button_off.png";
    const auto press_a_path = title_directory / "press_a_button.png";
    const auto menu_path = title_directory / "menu_options.png";
    const auto prompt_path = title_directory / "press_prompt.png";
    const auto fallback_title_path = title_directory / "title.png";

    Texture plane2;
    Texture plane1;
    Texture prompt;
    Texture press_a_off;
    Texture press_a;
    Texture menu;
    std::vector<Texture> sonic_frames;
    std::vector<Texture> wait_on_frames;
    std::vector<Texture> wait_off_frames;
    std::vector<Texture> intro_frames;
    const auto intro_directory = title_directory / "intro";
    const bool intro_is_teacher_capture =
        std::filesystem::is_regular_file(intro_directory / "teacher_capture.txt");
    if (std::filesystem::is_directory(intro_directory)) {
        for (int index = 0;; ++index) {
            std::ostringstream filename;
            filename << "frame_" << std::setw(4) << std::setfill('0') << index << ".png";
            const auto intro_path = intro_directory / filename.str();
            if (!std::filesystem::is_regular_file(intro_path)) {
                break;
            }
            Texture intro = load_png(app.renderer, intro_path);
            if (intro.value == nullptr) {
                return 1;
            }
            intro_frames.push_back(std::move(intro));
        }
    }
    if (std::filesystem::is_regular_file(plane2_path) &&
        std::filesystem::is_regular_file(plane1_path)) {
        plane2 = load_png(app.renderer, plane2_path);
        plane1 = load_png(app.renderer, plane1_path);
        if (std::filesystem::is_regular_file(press_a_off_path)) {
            press_a_off = load_png(app.renderer, press_a_off_path);
        }
        if (std::filesystem::is_regular_file(press_a_path)) {
            press_a = load_png(app.renderer, press_a_path);
        }
        if (std::filesystem::is_regular_file(menu_path)) {
            menu = load_png(app.renderer, menu_path);
        }
        if (std::filesystem::is_regular_file(prompt_path)) {
            prompt = load_png(app.renderer, prompt_path);
        }
        for (int index = 0;; ++index) {
            const auto sonic_path =
                title_directory / ("title_sonic_" + std::to_string(index) + ".png");
            if (!std::filesystem::is_regular_file(sonic_path)) {
                break;
            }
            Texture sonic = load_png(app.renderer, sonic_path);
            if (sonic.value == nullptr) {
                return 1;
            }
            sonic_frames.push_back(std::move(sonic));
        }
        auto load_wait_frames = [&](const std::filesystem::path& directory, std::vector<Texture>& frames) {
            if (!std::filesystem::is_directory(directory)) {
                return true;
            }
            for (int index = 0;; ++index) {
                std::ostringstream filename;
                filename << "frame_" << std::setw(4) << std::setfill('0') << index << ".png";
                const auto path = directory / filename.str();
                if (!std::filesystem::is_regular_file(path)) {
                    break;
                }
                Texture texture = load_png(app.renderer, path);
                if (texture.value == nullptr) {
                    return false;
                }
                frames.push_back(std::move(texture));
            }
            return true;
        };
        if (!load_wait_frames(title_directory / "wait_on", wait_on_frames) ||
            !load_wait_frames(title_directory / "wait_off", wait_off_frames)) {
            return 1;
        }
    } else {
        plane2 = load_png(app.renderer, fallback_title_path);
    }

    if (plane2.value == nullptr) {
        return 1;
    }
    SDL_SetWindowTitle(app.window, "Sonic Pocket - Title Screen");
    int frame = 0;
    auto title_sonic_texture = [&sonic_frames](int frame_number) -> SDL_Texture* {
        if (sonic_frames.empty()) {
            return nullptr;
        }
        constexpr std::array<int, 6> kTitleSonicDurations{4, 2, 2, 4, 2, 2};
        const int cycle_frames = std::accumulate(
            kTitleSonicDurations.begin(), kTitleSonicDurations.end(), 0);
        int cursor = frame_number % cycle_frames;
        for (std::size_t index = 0; index < sonic_frames.size() &&
             index < kTitleSonicDurations.size(); ++index) {
            if (cursor < kTitleSonicDurations[index]) {
                return sonic_frames[index].value;
            }
            cursor -= kTitleSonicDurations[index];
        }
        return sonic_frames.front().value;
    };
    auto title_sonic_index = [&sonic_frames](int frame_number) -> int {
        if (sonic_frames.empty()) {
            return 0;
        }
        constexpr std::array<int, 6> kTitleSonicDurations{4, 2, 2, 4, 2, 2};
        const int cycle_frames = std::accumulate(
            kTitleSonicDurations.begin(), kTitleSonicDurations.end(), 0);
        int cursor = frame_number % cycle_frames;
        for (std::size_t index = 0; index < sonic_frames.size() &&
             index < kTitleSonicDurations.size(); ++index) {
            if (cursor < kTitleSonicDurations[index]) {
                return static_cast<int>(index);
            }
            cursor -= kTitleSonicDurations[index];
        }
        return 0;
    };
    if (!intro_frames.empty()) {
        if (!render_title_intro_frame(app, intro_frames.front().value)) {
            return 1;
        }
    } else {
        if (!render_title_frame(
                app, plane2.value, plane1.value, title_sonic_texture(frame), prompt.value, true)) {
            return 1;
        }
    }
    if (smoke_test) {
        return 0;
    }

    bool running = true;
    bool playing_intro = !intro_frames.empty();
    bool showing_menu = false;
    int intro_frame = 0;
    int title_logic_tick = 0;
    constexpr Uint32 kTitleFrameDelayMs = 16;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN) {
                const SDL_Keycode key = event.key.key;
                if (
                    key == SDLK_ESCAPE ||
                    key == SDLK_RETURN ||
                    key == SDLK_SPACE ||
                    key == SDLK_Z) {
                    if (playing_intro && key != SDLK_ESCAPE) {
                        playing_intro = false;
                        frame = 0;
                    } else if (!showing_menu && key != SDLK_ESCAPE) {
                        showing_menu = true;
                        frame = 0;
                    } else {
                        running = false;
                    }
                }
            } else if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                if (
                    event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH ||
                    event.gbutton.button == SDL_GAMEPAD_BUTTON_START) {
                    if (playing_intro) {
                        playing_intro = false;
                        frame = 0;
                    } else if (!showing_menu) {
                        showing_menu = true;
                        frame = 0;
                    } else {
                        running = false;
                    }
                }
            }
        }
        if (playing_intro) {
            const int clamped_intro_frame =
                std::min<int>(intro_frame, static_cast<int>(intro_frames.size()) - 1);
            if (!render_title_intro_frame(app, intro_frames[clamped_intro_frame].value)) {
                return 1;
            }
            if (intro_is_teacher_capture) {
                if (intro_frame + 1 < static_cast<int>(intro_frames.size())) {
                    ++intro_frame;
                }
            } else {
                ++title_logic_tick;
                if ((title_logic_tick & 1) == 0) {
                    ++intro_frame;
                    if (intro_frame >= static_cast<int>(intro_frames.size())) {
                        playing_intro = false;
                        frame = 0;
                        title_logic_tick = 0;
                    }
                }
                if (intro_frame >= static_cast<int>(intro_frames.size())) {
                    playing_intro = false;
                    frame = 0;
                    title_logic_tick = 0;
                }
            }
            SDL_Delay(kTitleFrameDelayMs);
            continue;
        }
        SDL_Texture* overlay = nullptr;
        const bool prompt_on = press_a.value != nullptr && ((frame / 10) % 2 == 0);
        if (showing_menu) {
            overlay = menu.value;
        } else if (prompt_on) {
            overlay = press_a.value;
        } else if (press_a_off.value != nullptr) {
            overlay = press_a_off.value;
        } else if (press_a.value == nullptr) {
            overlay = prompt.value;
        }
        if (!showing_menu && !wait_on_frames.empty() && !wait_off_frames.empty()) {
            const auto& wait_frames = prompt_on ? wait_on_frames : wait_off_frames;
            const int sonic_index = title_sonic_index(frame);
            const int clamped_index =
                std::min<int>(sonic_index, static_cast<int>(wait_frames.size()) - 1);
            if (!render_title_intro_frame(app, wait_frames[clamped_index].value)) {
                return 1;
            }
            ++title_logic_tick;
            if ((title_logic_tick & 1) == 0) {
                ++frame;
            }
            SDL_Delay(kTitleFrameDelayMs);
            continue;
        }
        if (!render_title_frame(app, plane2.value, plane1.value, title_sonic_texture(frame), overlay, true)) {
            return 1;
        }
        ++title_logic_tick;
        if ((title_logic_tick & 1) == 0) {
            ++frame;
        }
        SDL_Delay(kTitleFrameDelayMs);
    }
    return 0;
}

std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, ',')) {
        fields.push_back(field);
    }
    return fields;
}

int parse_trace_int(const std::string& value) {
    return std::stoi(value, nullptr, 0);
}

int trace_field(
    const std::vector<std::string>& header,
    const std::vector<std::string>& row,
    std::string_view name,
    int fallback = 0) {
    const auto found = std::find(header.begin(), header.end(), std::string{name});
    if (found == header.end()) {
        return fallback;
    }
    const std::size_t index =
        static_cast<std::size_t>(std::distance(header.begin(), found));
    if (index >= row.size() || row[index].empty()) {
        return fallback;
    }
    return parse_trace_int(row[index]);
}

void load_player_from_trace_row(
    Player& player,
    const std::vector<std::string>& header,
    const std::vector<std::string>& row) {
    player = Player{};
    player.x_raw = trace_field(header, row, "x_raw_16_8");
    player.y_raw =
        ((kStageHeight - 1) * kFixedOne) -
        trace_field(header, row, "y_raw_16_8");
    player.ground_speed = trace_field(header, row, "ground_speed_s8_8");
    player.velocity_x = trace_field(header, row, "x_velocity_s8_8");
    player.velocity_y = -trace_field(header, row, "y_velocity_s8_8");
    player.previous_x_raw =
        trace_field(header, row, "previous_collision_x_word",
                    player.x_raw / kFixedOne) *
        kFixedOne;
    player.previous_y_raw =
        ((kStageHeight - 1) -
         trace_field(header, row, "previous_collision_y_word",
                     ((kStageHeight - 1) * kFixedOne - player.y_raw) /
                         kFixedOne)) *
        kFixedOne;
    player.body_half_width =
        trace_field(header, row, "collision_radius_x", player.body_half_width);
    player.body_half_height =
        trace_field(header, row, "collision_radius_y", player.body_half_height);
    player.collision_plane =
        trace_field(header, row, "collision_plane", player.collision_plane);
    player.ground_angle = trace_field(header, row, "surface_angle") & 0xFF;
    player.grounded = trace_field(header, row, "state") != 0x0039AAF7;
    player.facing_left =
        (trace_field(header, row, "movement_flags") & 0x80) != 0;
    player.walking_active = player.grounded && player.ground_speed != 0;
}

void write_trace_state(
    std::ofstream& output,
    std::size_t row_index,
    int frame,
    int buttons,
    const Player& player) {
    output
        << row_index << ','
        << frame << ','
        << "0x" << std::hex << buttons << std::dec << ','
        << player.x_raw << ','
        << (((kStageHeight - 1) * kFixedOne) - player.y_raw) << ','
        << player.ground_speed << ','
        << player.velocity_x << ','
        << -player.velocity_y << ','
        << "0x" << std::hex << (player.ground_angle & 0xFF) << std::dec << ','
        << (player.grounded ? 1 : 0) << ','
        << player.debug_walk_delta_x << ','
        << player.debug_walk_delta_y << ','
        << "0x" << std::hex << (player.debug_walk_angle & 0xFF) << std::dec << ','
        << player.debug_floor_probe_center_x << ','
        << player.debug_floor_probe_rom_y << ','
        << player.debug_floor_applied_delta_y << ','
        << "0x" << std::hex << (player.debug_floor_previous_angle & 0xFF) << std::dec << ','
        << player.debug_floor_velocity_x << ','
        << player.debug_floor_velocity_y << ','
        << player.debug_floor_hit_delta_y << ','
        << player.debug_floor_hit_response << ','
        << player.debug_floor_hit_local_x << ','
        << player.debug_floor_hit_local_y << ','
        << player.debug_floor_hit_collision_type << ','
        << "0x" << std::hex << (player.debug_no_ground_sector & 0xFF) << std::dec << ','
        << (player.debug_no_ground_hit ? 1 : 0) << ','
        << player.debug_no_ground_delta_x << ','
        << player.debug_no_ground_delta_y << ','
        << "0x" << std::hex << (player.debug_no_ground_angle & 0xFF) << std::dec
        << ','
        << (player.debug_no_ground_probe_a_hit ? 1 : 0) << ','
        << player.debug_no_ground_probe_a_delta << ','
        << "0x" << std::hex << (player.debug_no_ground_probe_a_angle & 0xFF) << std::dec
        << ','
        << (player.debug_no_ground_probe_b_hit ? 1 : 0) << ','
        << player.debug_no_ground_probe_b_delta << ','
        << "0x" << std::hex << (player.debug_no_ground_probe_b_angle & 0xFF) << std::dec
        << '\n';
}

std::filesystem::path resolve_replay_trace_path(std::filesystem::path trace_path) {
    if (trace_path.filename() != "player-runtime-trace.csv") {
        return trace_path;
    }

    const std::filesystem::path marker_path =
        trace_path.parent_path() / "player-runtime-trace-latest.txt";
    std::ifstream marker(marker_path);
    std::string latest_path;
    if (marker && std::getline(marker, latest_path) && !latest_path.empty()) {
        std::filesystem::path candidate{latest_path};
        if (std::filesystem::is_regular_file(candidate)) {
            return candidate;
        }
    }

    return trace_path;
}

int replay_trace(
    const CollisionMask& collision,
    const std::filesystem::path& trace_path,
    const std::filesystem::path& output_path) {
    const std::filesystem::path resolved_trace_path =
        resolve_replay_trace_path(trace_path);
    std::ifstream input(resolved_trace_path);
    if (!input) {
        std::cerr << "Unable to open replay trace " << resolved_trace_path << '\n';
        return 2;
    }
    std::ofstream output(output_path);
    if (!output) {
        std::cerr << "Unable to create native trace " << output_path << '\n';
        return 2;
    }

    std::string line;
    if (!std::getline(input, line)) {
        std::cerr << "Replay trace is empty: " << trace_path << '\n';
        return 2;
    }
    const std::vector<std::string> header = split_csv_line(line);
    if (header.empty()) {
        std::cerr << "Replay trace has no header: " << trace_path << '\n';
        return 2;
    }

    std::vector<std::vector<std::string>> rows;
    while (std::getline(input, line)) {
        if (!line.empty()) {
            rows.push_back(split_csv_line(line));
        }
    }
    if (rows.empty()) {
        std::cerr << "Replay trace has no rows: " << trace_path << '\n';
        return 2;
    }

    Player player;
    const auto& first = rows.front();
    player.x_raw = trace_field(header, first, "x_raw_16_8");
    player.y_raw =
        ((kStageHeight - 1) * kFixedOne) -
        trace_field(header, first, "y_raw_16_8");
    player.ground_speed = trace_field(header, first, "ground_speed_s8_8");
    player.velocity_x = trace_field(header, first, "x_velocity_s8_8");
    player.velocity_y = -trace_field(header, first, "y_velocity_s8_8");
    player.ground_angle = trace_field(header, first, "surface_angle") & 0xFF;
    player.grounded = trace_field(header, first, "state") != 0x0039AAF7;
    player.facing_left = (trace_field(header, first, "movement_flags") & 0x80) != 0;
    player.walking_active = player.ground_speed != 0;

    output
        << "row,frame,buttons_current,x_raw_16_8,y_raw_16_8,"
           "ground_speed_s8_8,x_velocity_s8_8,y_velocity_s8_8,"
           "surface_angle,grounded,walk_delta_x,walk_delta_y,walk_angle,"
           "floor_probe_center_x,floor_probe_rom_y,floor_applied_delta_y,"
           "floor_previous_angle,floor_velocity_x,floor_velocity_y,"
           "floor_hit_delta_y,floor_hit_response,floor_hit_local_x,"
           "floor_hit_local_y,floor_hit_collision_type,"
           "no_ground_sector,no_ground_hit,no_ground_delta_x,no_ground_delta_y,"
           "no_ground_angle,no_ground_probe_a_hit,no_ground_probe_a_delta,"
           "no_ground_probe_a_angle,no_ground_probe_b_hit,no_ground_probe_b_delta,"
           "no_ground_probe_b_angle\n";

    int previous_logic_buttons = trace_field(header, first, "buttons_current");
    constexpr int kButtonLeft = 0x04;
    constexpr int kButtonRight = 0x08;
    constexpr int kButtonJump = 0x10;
    constexpr int kButtonUp = 0x20;
    constexpr int kButtonDown = 0x40;
    constexpr int kTraceLogicCadence = 2;

    for (std::size_t row_index = 0; row_index < rows.size(); ++row_index) {
        const auto& row = rows[row_index];
        const int frame = trace_field(header, row, "frame");
        const int buttons = trace_field(header, row, "buttons_current");
        write_trace_state(output, row_index, frame, buttons, player);
        if ((frame % kTraceLogicCadence) == 1) {
            const auto& logic_row =
                row_index + 1 < rows.size() ? rows[row_index + 1] : row;
            const int next_buttons =
                trace_field(header, logic_row, "buttons_current");
            const int logic_buttons =
                player.ground_speed == 0 ? (buttons & next_buttons) : next_buttons;
            const int movement =
                ((logic_buttons & kButtonRight) != 0) -
                ((logic_buttons & kButtonLeft) != 0);
            const bool jump_pressed =
                (logic_buttons & kButtonJump) != 0 &&
                (previous_logic_buttons & kButtonJump) == 0;
            update_player(
                player,
                collision,
                movement,
                jump_pressed,
                (logic_buttons & kButtonJump) != 0,
                (logic_buttons & kButtonUp) != 0,
                (logic_buttons & kButtonDown) != 0);
            previous_logic_buttons = logic_buttons;
        }
    }

    std::cout << "Native replay used " << resolved_trace_path << '\n';
    std::cout << "Native replay trace wrote " << output_path
              << " from " << rows.size() << " rows\n";
    return 0;
}

int teacher_trace(
    const CollisionMask& collision,
    const std::filesystem::path& trace_path,
    const std::filesystem::path& output_path) {
    const std::filesystem::path resolved_trace_path =
        resolve_replay_trace_path(trace_path);
    std::ifstream input(resolved_trace_path);
    if (!input) {
        std::cerr << "Unable to open teacher trace " << resolved_trace_path << '\n';
        return 2;
    }
    std::ofstream output(output_path);
    if (!output) {
        std::cerr << "Unable to create teacher output " << output_path << '\n';
        return 2;
    }

    std::string line;
    if (!std::getline(input, line)) {
        std::cerr << "Teacher trace is empty: " << trace_path << '\n';
        return 2;
    }
    const std::vector<std::string> header = split_csv_line(line);

    std::vector<std::vector<std::string>> rows;
    while (std::getline(input, line)) {
        if (!line.empty()) {
            rows.push_back(split_csv_line(line));
        }
    }
    if (rows.size() < 2) {
        std::cerr << "Teacher trace needs at least two rows: " << trace_path << '\n';
        return 2;
    }

    output
        << "row,frame,buttons_current,movement,jump_pressed,"
           "expected_x_raw_16_8,actual_x_raw_16_8,dx_raw,"
           "expected_y_raw_16_8,actual_y_raw_16_8,dy_raw,"
           "expected_ground_speed_s8_8,actual_ground_speed_s8_8,dground_speed,"
           "expected_x_velocity_s8_8,actual_x_velocity_s8_8,dx_velocity,"
           "expected_y_velocity_s8_8,actual_y_velocity_s8_8,dy_velocity,"
           "expected_surface_angle,actual_surface_angle,"
           "expected_grounded,actual_grounded,"
           "walk_delta_x,walk_delta_y,walk_angle,"
           "floor_probe_center_x,floor_probe_rom_y,floor_applied_delta_y,"
           "floor_previous_angle,floor_velocity_x,floor_velocity_y,"
           "floor_hit_delta_y,floor_hit_response,floor_hit_local_x,"
           "floor_hit_local_y,floor_hit_collision_type,"
           "no_ground_sector,no_ground_hit,no_ground_delta_x,no_ground_delta_y,"
           "no_ground_angle,no_ground_probe_a_hit,no_ground_probe_a_delta,"
           "no_ground_probe_a_angle,no_ground_probe_b_hit,no_ground_probe_b_delta,"
           "no_ground_probe_b_angle\n";

    constexpr int kButtonLeft = 0x04;
    constexpr int kButtonRight = 0x08;
    constexpr int kButtonJump = 0x10;
    constexpr int kButtonUp = 0x20;
    constexpr int kButtonDown = 0x40;
    constexpr int kTraceLogicCadence = 2;

    std::size_t samples = 0;
    for (std::size_t row_index = 0; row_index + 1 < rows.size(); ++row_index) {
        const auto& row = rows[row_index];
        const int frame = trace_field(header, row, "frame");
        if ((frame % kTraceLogicCadence) != 1) {
            continue;
        }

        const auto& next_row = rows[row_index + 1];
        Player player;
        load_player_from_trace_row(player, header, row);

        const int buttons = trace_field(header, row, "buttons_current");
        const int next_buttons = trace_field(header, next_row, "buttons_current");
        const int pressed = trace_field(header, next_row, "buttons_pressed");
        const int logic_buttons =
            player.ground_speed == 0 ? (buttons & next_buttons) : next_buttons;
        const int movement =
            ((logic_buttons & kButtonRight) != 0) -
            ((logic_buttons & kButtonLeft) != 0);
        const bool jump_pressed = (pressed & kButtonJump) != 0;

        update_player(
            player,
            collision,
            movement,
            jump_pressed,
            (logic_buttons & kButtonJump) != 0,
            (logic_buttons & kButtonUp) != 0,
            (logic_buttons & kButtonDown) != 0);

        const int expected_x = trace_field(header, next_row, "x_raw_16_8");
        const int actual_x = player.x_raw;
        const int expected_y = trace_field(header, next_row, "y_raw_16_8");
        const int actual_y = ((kStageHeight - 1) * kFixedOne) - player.y_raw;
        const int expected_ground_speed =
            trace_field(header, next_row, "ground_speed_s8_8");
        const int actual_ground_speed = player.ground_speed;
        const int expected_velocity_x =
            trace_field(header, next_row, "x_velocity_s8_8");
        const int actual_velocity_x = player.velocity_x;
        const int expected_velocity_y =
            trace_field(header, next_row, "y_velocity_s8_8");
        const int actual_velocity_y = -player.velocity_y;
        const int expected_angle =
            trace_field(header, next_row, "surface_angle") & 0xFF;
        const int actual_angle = player.ground_angle & 0xFF;
        const bool expected_grounded =
            trace_field(header, next_row, "state") != 0x0039AAF7;

        output
            << row_index << ','
            << frame << ','
            << "0x" << std::hex << logic_buttons << std::dec << ','
            << movement << ','
            << (jump_pressed ? 1 : 0) << ','
            << expected_x << ','
            << actual_x << ','
            << (actual_x - expected_x) << ','
            << expected_y << ','
            << actual_y << ','
            << (actual_y - expected_y) << ','
            << expected_ground_speed << ','
            << actual_ground_speed << ','
            << (actual_ground_speed - expected_ground_speed) << ','
            << expected_velocity_x << ','
            << actual_velocity_x << ','
            << (actual_velocity_x - expected_velocity_x) << ','
            << expected_velocity_y << ','
            << actual_velocity_y << ','
            << (actual_velocity_y - expected_velocity_y) << ','
            << "0x" << std::hex << expected_angle << std::dec << ','
            << "0x" << std::hex << actual_angle << std::dec << ','
            << (expected_grounded ? 1 : 0) << ','
            << (player.grounded ? 1 : 0) << ','
            << player.debug_walk_delta_x << ','
            << player.debug_walk_delta_y << ','
            << "0x" << std::hex << (player.debug_walk_angle & 0xFF) << std::dec << ','
            << player.debug_floor_probe_center_x << ','
            << player.debug_floor_probe_rom_y << ','
            << player.debug_floor_applied_delta_y << ','
            << "0x" << std::hex << (player.debug_floor_previous_angle & 0xFF) << std::dec << ','
            << player.debug_floor_velocity_x << ','
            << player.debug_floor_velocity_y << ','
            << player.debug_floor_hit_delta_y << ','
            << player.debug_floor_hit_response << ','
            << player.debug_floor_hit_local_x << ','
            << player.debug_floor_hit_local_y << ','
            << player.debug_floor_hit_collision_type << ','
            << "0x" << std::hex << (player.debug_no_ground_sector & 0xFF) << std::dec << ','
            << (player.debug_no_ground_hit ? 1 : 0) << ','
            << player.debug_no_ground_delta_x << ','
            << player.debug_no_ground_delta_y << ','
            << "0x" << std::hex << (player.debug_no_ground_angle & 0xFF) << std::dec
            << ','
            << (player.debug_no_ground_probe_a_hit ? 1 : 0) << ','
            << player.debug_no_ground_probe_a_delta << ','
            << "0x" << std::hex << (player.debug_no_ground_probe_a_angle & 0xFF) << std::dec
            << ','
            << (player.debug_no_ground_probe_b_hit ? 1 : 0) << ','
            << player.debug_no_ground_probe_b_delta << ','
            << "0x" << std::hex << (player.debug_no_ground_probe_b_angle & 0xFF) << std::dec
            << '\n';
        ++samples;
    }

    std::cout << "Teacher trace used " << resolved_trace_path << '\n';
    std::cout << "Teacher-forced output wrote " << output_path
              << " from " << samples << " logic samples\n";
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    bool smoke_test = false;
    bool title_screen = false;
    std::filesystem::path requested_data;
    std::filesystem::path replay_trace_path;
    std::filesystem::path trace_output_path;
    std::filesystem::path teacher_trace_path;
    std::filesystem::path teacher_output_path;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--smoke-test") {
            smoke_test = true;
        } else if (argument == "--title-screen") {
            title_screen = true;
        } else if (argument == "--replay-trace" && index + 1 < argc) {
            replay_trace_path = argv[++index];
        } else if (argument == "--trace-out" && index + 1 < argc) {
            trace_output_path = argv[++index];
        } else if (argument == "--teacher-trace" && index + 1 < argc) {
            teacher_trace_path = argv[++index];
        } else if (argument == "--teacher-out" && index + 1 < argc) {
            teacher_output_path = argv[++index];
        } else if (requested_data.empty()) {
            requested_data = argv[index];
        } else {
            std::cerr
                << "Usage: sonic-pocket-viewer [data-directory] [--smoke-test] "
                   "[--title-screen] "
                   "[--replay-trace trace.csv --trace-out native.csv] "
                   "[--teacher-trace trace.csv --teacher-out teacher.csv]\n";
            return 2;
        }
    }
    if (!replay_trace_path.empty() && trace_output_path.empty()) {
        trace_output_path = "out/native-runtime-trace.csv";
    }
    if (!teacher_trace_path.empty() && teacher_output_path.empty()) {
        teacher_output_path = "out/native-teacher-trace.csv";
    }

    if (title_screen) {
        std::filesystem::path title_directory = requested_data.empty()
            ? std::filesystem::path{"out/title"}
            : requested_data;
        if (std::filesystem::is_regular_file(title_directory)) {
            title_directory = title_directory.parent_path();
        }
        if (!std::filesystem::is_regular_file(title_directory / "title.png")) {
            std::cerr << "Missing extracted title data in " << title_directory << '\n'
                      << "Run: py -3 tools/extract_title.py\n";
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
                "Sonic Pocket - Title Screen",
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
        return run_title_screen(app, title_directory, smoke_test);
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

    CollisionMask collision_mask = load_collision_mask(collision_mask_path);
    if (collision_mask.pixels.empty()) {
        return 1;
    }
    if (!replay_trace_path.empty()) {
        return replay_trace(collision_mask, replay_trace_path, trace_output_path);
    }
    if (!teacher_trace_path.empty()) {
        return teacher_trace(collision_mask, teacher_trace_path, teacher_output_path);
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
             10.0F, 10.0F, 10.0F, 10.0F, 35.0F, 20.0F},
            kPlayerHalfHeight),
        load_animation_sequence(
            app.renderer,
            data_directory,
            "walk",
            {4.0F, 4.0F, 4.0F, 4.0F, 4.0F, 4.0F, 4.0F, 4.0F},
            kPlayerHalfHeight),
        load_animation_sequence(
            app.renderer,
            data_directory,
            "run",
            {2.0F, 2.0F, 2.0F, 2.0F, 2.0F, 2.0F, 2.0F, 2.0F},
            kPlayerHalfHeight),
        load_animation_sequence(
            app.renderer,
            data_directory,
            "peelout",
            {1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F},
            kPlayerHalfHeight),
        load_animation_sequence(
            app.renderer, data_directory, "skid", {20.0F, 3.0F}, kPlayerHalfHeight),
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
        load_animation_sequence(
            app.renderer,
            data_directory,
            "push",
            {12.0F, 12.0F, 12.0F, 12.0F},
            kPlayerHalfHeight),
        load_animation_sequence(
            app.renderer, data_directory, "look_up", {10.0F, 10.0F, 10.0F}, kPlayerHalfHeight),
        load_animation_sequence(
            app.renderer, data_directory, "look_down", {4.0F, 4.0F, 4.0F}, kPlayerHalfHeight),
        load_animation_sequence(
            app.renderer, data_directory, "balance", {10.0F, 10.0F}, kPlayerHalfHeight),
    };
    AnimationSequence skid_dust = load_effect_sequence(
        app.renderer,
        data_directory,
        "skid_dust",
        {2.0F, 2.0F, 2.0F, 2.0F},
        {{3.0F, 4.0F}, {4.0F, 4.0F}, {3.0F, 4.0F}, {4.0F, 4.0F}});
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
        !animation_sequence_loaded(skid_dust)) {
        return 1;
    }

    Player player;
    reset_player(player, collision_mask);
    prime_player_ground_contact(player, collision_mask);
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
    std::ofstream collision_debug{"out/native-collision-debug.csv"};
    if (collision_debug) {
        collision_debug
            << "frame,movement,jump_pressed,jump_held,was_grounded,grounded,"
               "left_ground,x_raw_16_8,y_raw_16_8,x_integer,y_integer,"
               "previous_x_raw_16_8,previous_y_raw_16_8,ground_speed_s8_8,"
               "x_velocity_s8_8,y_velocity_s8_8,surface_angle,collision_radius_x,"
               "collision_radius_y,scan_x_length,scan_y_length,rom_floor_probe_y,"
               "has_support,no_ground_recovered,landing_contact,"
               "floor_left_hit,floor_left_delta_y,floor_left_angle,"
               "floor_left_response,floor_left_collision_type,floor_left_local_x,"
               "floor_left_local_y,floor_right_hit,floor_right_delta_y,"
               "floor_right_angle,floor_right_response,floor_right_collision_type,"
               "floor_right_local_x,floor_right_local_y,no_ground_x_raw_16_8,"
               "no_ground_y_raw_16_8,no_ground_angle,landing_x_raw_16_8,"
               "landing_y_raw_16_8,landing_angle\n";
    } else {
        std::cerr << "Warning: could not open out/native-collision-debug.csv\n";
    }
    bool running = true;
    bool show_collision = false;
    bool jump_pressed = false;
    int debug_frame = 0;
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
                        prime_player_ground_contact(player, collision_mask);
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
                        prime_player_ground_contact(player, collision_mask);
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
            const int step_movement = std::clamp(movement_x, -1, 1);
            const bool step_jump_pressed = jump_pressed;
            const bool was_grounded = player.grounded;
            update_player(
                player, collision_mask,
                step_movement,
                step_jump_pressed,
                jump_held,
                input_up,
                input_down);
            if (collision_debug) {
                write_collision_debug_row(
                    collision_debug,
                    debug_frame,
                    player,
                    collision_mask,
                    step_movement,
                    step_jump_pressed,
                    jump_held,
                    was_grounded);
                collision_debug.flush();
            }
            jump_pressed = false;
            update_animation(player, sonic_animations, kAnimationTickSeconds);
            update_camera(camera_x, camera_y, camera_follow_x, player);
            simulation_accumulator -= kFixedFrameSeconds;
            ++simulation_steps;
            ++debug_frame;
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
