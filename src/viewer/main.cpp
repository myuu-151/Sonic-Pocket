#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr int kLogicalWidth = 160;
constexpr int kLogicalHeight = 152;
constexpr int kDefaultWindowScale = 3;
constexpr int kStageWidth = 6400;
constexpr int kStageHeight = 992;
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
constexpr int kAirAcceleration = 0x10;
constexpr int kAirMaxXSpeed = 0x800;
constexpr int kGravity = 0x80;
constexpr int kFallMaxSpeed = 0xF00;
constexpr int kJumpImpulse = 0x900;
constexpr int kJumpReleaseLimit = 0x400;

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
    AnimationSequence run;
    AnimationSequence jump;
    AnimationSequence fall;
};

enum class AnimationState {
    Idle,
    Run,
    Jump,
    Fall,
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

struct Player {
    int x_raw = static_cast<int>(kPlayerStartX * kFixedOne);
    int y_raw = 0;
    int ground_speed = 0;
    int velocity_x = 0;
    int velocity_y = 0;
    bool grounded = false;
    bool facing_left = false;
    bool jump_held = false;
    AnimationState animation_state = AnimationState::Idle;
    float animation_time = 0.0F;
    float air_time = 0.0F;
    std::size_t animation_frame = 0;

    float x() const { return static_cast<float>(x_raw) / kFixedOne; }
    float y() const { return static_cast<float>(y_raw) / kFixedOne; }
};

struct CollisionMask {
    std::vector<unsigned char> pixels;

    bool solid(int x, int y) const {
        if (x < 0 || x >= kStageWidth || y < 0 || y >= kStageHeight) {
            return true;
        }
        return pixels[static_cast<std::size_t>(y) * kStageWidth + x] != 0;
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
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        std::cerr << "Unable to load " << path << '\n';
        return {};
    }
    CollisionMask result;
    result.pixels.assign(
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>());
    const auto expected =
        static_cast<std::size_t>(kStageWidth) * kStageHeight;
    if (result.pixels.size() != expected) {
        std::cerr << "Collision mask has " << result.pixels.size()
                  << " bytes; expected " << expected << '\n';
        result.pixels.clear();
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

void center_camera(float& camera_x, float& camera_y, const Player& player) {
    camera_x = std::clamp(
        player.x() - 48.0F,
        kCameraMinX,
        kCameraMaxX);
    camera_y = std::clamp(
        player.y() - 76.0F,
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
        case AnimationState::Run:
            return "run";
        case AnimationState::Jump:
            return "jump";
        case AnimationState::Fall:
            return "fall";
    }
    return "unknown";
}

AnimationState choose_animation_state(const Player& player) {
    if (!player.grounded && player.air_time >= kAnimationAirThreshold) {
        return player.velocity_y < 0 ? AnimationState::Jump : AnimationState::Fall;
    }
    if (std::abs(player.ground_speed) > 0x100 ||
        std::abs(player.velocity_x) > 0x100) {
        return AnimationState::Run;
    }
    return AnimationState::Idle;
}

const AnimationSequence& animation_sequence(
    const SonicAnimations& animations, AnimationState state) {
    switch (state) {
        case AnimationState::Idle:
            return animations.idle;
        case AnimationState::Run:
            return animations.run;
        case AnimationState::Jump:
            return animations.jump;
        case AnimationState::Fall:
            return animations.fall;
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
    player.animation_state = AnimationState::Idle;
    player.animation_time = 0.0F;
    player.air_time = 0.0F;
    player.animation_frame = 0;
}

int approach_fixed(int value, int target, int amount) {
    if (value < target) {
        return std::min(value + amount, target);
    }
    return std::max(value - amount, target);
}

void update_player(Player& player, const CollisionMask& collision,
                   int movement, bool jump_pressed, bool jump_held) {
    player.jump_held = jump_held;

    if (player.grounded) {
        if (movement > 0) {
            player.ground_speed = std::min(
                player.ground_speed + kGroundAcceleration, kGroundMaxSpeed);
            player.facing_left = false;
        } else if (movement < 0) {
            player.ground_speed = std::max(
                player.ground_speed - kGroundAcceleration, -kGroundMaxSpeed);
            player.facing_left = true;
        } else {
            player.ground_speed =
                approach_fixed(player.ground_speed, 0, kGroundFriction);
        }
        player.velocity_x = player.ground_speed;
        player.velocity_y = 0;
    } else {
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
        player.velocity_x = player.ground_speed;
        player.grounded = false;
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

    const int previous_y_raw = player.y_raw;
    player.y_raw += player.velocity_y;
    const float player_x = player.x();
    const float player_y = player.y();
    const int left = static_cast<int>(std::floor(player_x - kPlayerHalfWidth));
    const int center = static_cast<int>(std::floor(player_x));
    const int right = static_cast<int>(std::floor(player_x + kPlayerHalfWidth));

    if (player.velocity_y >= 0) {
        const int scan_start = static_cast<int>(
            std::floor(static_cast<float>(previous_y_raw) / kFixedOne +
                       kPlayerHalfHeight - 2.0F));
        const int scan_end = static_cast<int>(
            std::ceil(player_y + kPlayerHalfHeight + 3.0F));
        int surface = -1;
        for (const int sensor_x : {left, center, right}) {
            const int hit = collision.first_solid_y(
                sensor_x, scan_start, scan_end);
            if (hit >= 0 && (surface < 0 || hit < surface)) {
                surface = hit;
            }
        }
        if (surface >= 0) {
            player.y_raw = static_cast<int>(
                (static_cast<float>(surface) - kPlayerHalfHeight) * kFixedOne);
            player.velocity_y = 0;
            player.ground_speed = player.velocity_x;
            player.grounded = true;
            player.air_time = 0.0F;
        } else {
            player.grounded = false;
        }
    }

    if (player.grounded) {
        int surface = -1;
        for (const int sensor_x : {left, center, right}) {
            const int hit = collision.first_solid_y(
                sensor_x,
                static_cast<int>(player.y() + kPlayerHalfHeight - 5.0F),
                static_cast<int>(player.y() + kPlayerHalfHeight + 7.0F));
            if (hit >= 0 && (surface < 0 || hit < surface)) {
                surface = hit;
            }
        }
        if (surface >= 0) {
            player.y_raw = static_cast<int>(
                (static_cast<float>(surface) - kPlayerHalfHeight) * kFixedOne);
            player.air_time = 0.0F;
        } else {
            player.grounded = false;
        }
    }

    if (!player.grounded) {
        player.air_time += 1.0F / 60.0F;
    }
}

void update_camera(float& camera_x, float& camera_y, const Player& player,
                   float delta_seconds) {
    const float target_x = std::clamp(
        player.x() - (player.facing_left ? 112.0F : 48.0F),
        kCameraMinX,
        kCameraMaxX);
    const float target_y = std::clamp(
        player.y() - 76.0F, kCameraMinY, kCameraMaxY);
    camera_x = approach(camera_x, target_x, 150.0F * delta_seconds);
    camera_y = approach(camera_y, target_y, 150.0F * delta_seconds);
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
        load_animation_sequence(app.renderer, data_directory, "idle", {60.0F, 3.0F, 20.0F}),
        load_animation_sequence(
            app.renderer,
            data_directory,
            "run",
            {2.0F, 2.0F, 2.0F, 2.0F, 2.0F, 2.0F, 2.0F, 2.0F}),
        load_animation_sequence(app.renderer, data_directory, "jump", {1.0F}),
        load_animation_sequence(app.renderer, data_directory, "fall", {1.0F}),
    };
    CollisionMask collision_mask = load_collision_mask(collision_mask_path);
    if (stage.value == nullptr || collision.value == nullptr ||
        !animation_sequence_loaded(sonic_animations.idle) ||
        !animation_sequence_loaded(sonic_animations.run) ||
        !animation_sequence_loaded(sonic_animations.jump) ||
        !animation_sequence_loaded(sonic_animations.fall) ||
        collision_mask.pixels.empty()) {
        return 1;
    }

    Player player;
    reset_player(player, collision_mask);
    float camera_x = 0.0F;
    float camera_y = 0.0F;
    center_camera(camera_x, camera_y, player);

    if (smoke_test) {
        if (!render_frame(
                app, stage.value, collision.value,
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
                        center_camera(camera_x, camera_y, player);
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
                        center_camera(camera_x, camera_y, player);
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

        const bool* keyboard = SDL_GetKeyboardState(nullptr);
        int movement_x = 0;
        movement_x += keyboard[SDL_SCANCODE_RIGHT] || keyboard[SDL_SCANCODE_D];
        movement_x -= keyboard[SDL_SCANCODE_LEFT] || keyboard[SDL_SCANCODE_A];
        bool jump_held =
            keyboard[SDL_SCANCODE_SPACE] || keyboard[SDL_SCANCODE_Z];

        if (app.gamepad != nullptr) {
            const float analog_x = normalized_axis(SDL_GetGamepadAxis(
                app.gamepad, SDL_GAMEPAD_AXIS_LEFTX));
            movement_x += analog_x > 0.35F;
            movement_x -= analog_x < -0.35F;
            movement_x += SDL_GetGamepadButton(
                app.gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
            movement_x -= SDL_GetGamepadButton(
                app.gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
            jump_held = jump_held ||
                SDL_GetGamepadButton(app.gamepad, SDL_GAMEPAD_BUTTON_SOUTH);
        }

        update_player(
            player, collision_mask,
            std::clamp(movement_x, -1, 1),
            jump_pressed,
            jump_held);
        jump_pressed = false;
        update_animation(player, sonic_animations, delta_seconds);
        update_camera(camera_x, camera_y, player, delta_seconds);
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
