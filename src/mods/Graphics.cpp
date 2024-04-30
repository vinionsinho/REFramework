#include <utility/Module.hpp>
#include <utility/Scan.hpp>

#include <sdk/SceneManager.hpp>

#include "VR.hpp"
#include "Graphics.hpp"

#if TDB_VER <= 49
#include "sdk/regenny/re7/via/Window.hpp"
#include "sdk/regenny/re7/via/SceneView.hpp"
#elif TDB_VER < 69
#include "sdk/regenny/re3/via/Window.hpp"
#include "sdk/regenny/re3/via/SceneView.hpp"
#elif TDB_VER == 69
#include "sdk/regenny/re8/via/Window.hpp"
#include "sdk/regenny/re8/via/SceneView.hpp"
#elif TDB_VER == 70
#include "sdk/regenny/re2_tdb70/via/Window.hpp"
#include "sdk/regenny/re2_tdb70/via/SceneView.hpp"
#elif TDB_VER >= 71
#ifdef SF6
#include "sdk/regenny/sf6/via/Window.hpp"
#include "sdk/regenny/sf6/via/SceneView.hpp"
#elif defined(RE4)
#include "sdk/regenny/re4/via/Window.hpp"
#include "sdk/regenny/re4/via/SceneView.hpp"
#elif defined(DD2)
#include "sdk/regenny/dd2/via/Window.hpp"
#include "sdk/regenny/dd2/via/SceneView.hpp"
#else
#include "sdk/regenny/mhrise_tdb71/via/Window.hpp"
#include "sdk/regenny/mhrise_tdb71/via/SceneView.hpp"
#endif
#endif

std::shared_ptr<Graphics>& Graphics::get() {
    static auto mod = std::make_shared<Graphics>();
    return mod;
}

void Graphics::on_config_load(const utility::Config& cfg) {
    for (IModValue& option : m_options) {
        option.config_load(cfg);
    }
}

void Graphics::on_config_save(utility::Config& cfg) {
    for (IModValue& option : m_options) {
        option.config_save(cfg);
    }
}

void Graphics::on_frame() {
    if (m_disable_gui_key->is_key_down_once()) {
        m_disable_gui->toggle();
    }

#if TDB_VER >= 69
    if (m_ray_tracing_tweaks->value()) {
        setup_path_trace_hook();
        apply_ray_tracing_tweaks();
    }
#endif
}

void Graphics::on_draw_ui() {
    ImGui::SetNextItemOpen(false, ImGuiCond_::ImGuiCond_FirstUseEver);
    if (!ImGui::CollapsingHeader(get_name().data())) {
        return;
    }

#ifdef RE4
    ImGui::SetNextItemOpen(true, ImGuiCond_::ImGuiCond_Once);
    if (ImGui::TreeNode("RE4 Scope Tweaks")) {
        m_scope_tweaks->draw("Enable Scope Tweaks");

        if (m_scope_tweaks->value()) {
            m_scope_interlaced_rendering->draw("Enable Interlaced Rendering");
            m_scope_image_quality->draw("Scope Image Quality");
        }

        ImGui::TreePop();
    }
#endif

    ImGui::SetNextItemOpen(true, ImGuiCond_::ImGuiCond_Once);
    if (ImGui::TreeNode("Ultrawide/FOV Options")) {
        if (m_ultrawide_fix->draw("Ultrawide/FOV/Aspect Ratio Fix") && m_ultrawide_fix->value() == false) {
            do_ultrawide_fov_restore(true);
        }

        if (m_ultrawide_fix->value()) {
            m_ultrawide_vertical_fov->draw("Ultrawide: Enable Vertical FOV");
            m_ultrawide_custom_fov->draw("Ultrawide: Override FOV");
            m_ultrawide_fov_multiplier->draw("Ultrawide: FOV Multiplier");
        }

        m_force_render_res_to_window->draw("Force Render Resolution to Window Size");

        ImGui::TreePop();
    }

    ImGui::SetNextItemOpen(true, ImGuiCond_::ImGuiCond_Once);
    if (ImGui::TreeNode("GUI Options")) {
        m_disable_gui->draw("Hide GUI");
        m_disable_gui_key->draw("Hide GUI key");
        ImGui::TreePop();
    }

#if TDB_VER >= 69
    ImGui::SetNextItemOpen(true, ImGuiCond_::ImGuiCond_Once);
    if (ImGui::TreeNode("Ray Tracing Tweaks")) {
        m_ray_tracing_tweaks->draw("Enable Ray Tracing Tweaks");

        if (m_ray_tracing_tweaks->value()) {
            m_ray_trace_type->draw("Ray Trace Type");

            const auto clone_tooltip = 
                    "Can draw another RT pass over the main RT pass. Useful for hybrid rendering.\n"
                    "Example: Set Ray Trace Type to Pure and Ray Trace Clone Type to ASVGF. This adds RTGI to the path traced image.\n"
                    "Path Space Filter is also another good alternative for RTGI but it costs more performance.\n";

            m_ray_trace_clone_type_pre->draw("Ray Trace Clone Type Pre");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(clone_tooltip);
            }

            m_ray_trace_clone_type_post->draw("Ray Trace Clone Type Post");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(clone_tooltip);
            }
            
            m_ray_trace_clone_type_true->draw("Ray Trace Clone Type True");
            if (ImGui::IsItemHovered()) {
                const auto true_tooltip =
                    "Uses a completely separate RT component instead of re-using the main RT component.\n"
                    "Might crash or have other issues. Use with caution.\n";
                ImGui::SetTooltip(true_tooltip);
            }

            // Hybrid/pure
            if (m_ray_trace_type->value() == (int32_t)RayTraceType::Hybrid || m_ray_trace_type->value() == (int32_t)RayTraceType::Pure
                || m_ray_trace_clone_type_true->value() == (int32_t)RayTraceType::Hybrid || m_ray_trace_clone_type_true->value() == (int32_t)RayTraceType::Pure)
            {
                m_bounce_count->draw("Bounce Count");
                m_samples_per_pixel->draw("Samples Per Pixel");
            }
        }

        ImGui::TreePop();
    }
#endif
}

void Graphics::on_present() {
    if (g_framework->is_dx11()) {
        const auto& hook = g_framework->get_d3d11_hook();
        const auto swapchain = hook->get_swap_chain();

        if (swapchain == nullptr) {
            return;
        }

        Microsoft::WRL::ComPtr<ID3D11Texture2D> backbuffer{};
        if (FAILED(swapchain->GetBuffer(0, IID_PPV_ARGS(&backbuffer))) || backbuffer == nullptr) {
            return;
        }

        D3D11_TEXTURE2D_DESC desc{};
        backbuffer->GetDesc(&desc);

        const auto width = desc.Width;
        const auto height = desc.Height;

        if (m_backbuffer_size.has_value()) {
            (*m_backbuffer_size)[0] = width;
            (*m_backbuffer_size)[1] = height;
        } else {
            m_backbuffer_size = std::array<uint32_t, 2>{width, height};
        }
    } else {
        const auto& hook = g_framework->get_d3d12_hook();
        const auto swapchain = hook->get_swap_chain();

        if (swapchain == nullptr) {
            return;
        }

        Microsoft::WRL::ComPtr<ID3D12Resource> backbuffer{};
        if (FAILED(swapchain->GetBuffer(0, IID_PPV_ARGS(&backbuffer))) || backbuffer == nullptr) {
            return;
        }

        const auto desc = backbuffer->GetDesc();
        const auto width = (uint32_t)desc.Width;
        const auto height = (uint32_t)desc.Height;

        if (m_backbuffer_size.has_value()) {
            (*m_backbuffer_size)[0] = width;
            (*m_backbuffer_size)[1] = height;
        } else {
            m_backbuffer_size = std::array<uint32_t, 2>{width, height};
        }
    }
}

void Graphics::on_pre_application_entry(void* entry, const char* name, size_t hash) {
    // To fix the world-space GUI icons.
    if (hash == "UpdateBehavior"_fnv) {
        // SF6 has some weird behavior where it doesn't restore the FOV correctly
        // corrupting the value
#ifndef SF6
        do_ultrawide_fix();
#endif
    }

    if (hash == "UnlockScene"_fnv) {
        do_ultrawide_fov_restore();
    }
}

void Graphics::on_application_entry(void* entry, const char* name, size_t hash) {
    if (hash == "UpdateBehavior"_fnv) {
#ifndef SF6
        do_ultrawide_fov_restore();
#endif
    }

    // To actually fix the rendering.
    if (hash == "LockScene"_fnv) {
        do_ultrawide_fix();
    }
}

void Graphics::fix_ui_element(REComponent* gui_element) {
    if (gui_element == nullptr) {
        return;
    }

    auto game_object = utility::re_component::get_game_object(gui_element);

    if (game_object == nullptr || game_object->transform == nullptr) {
        return;
    }

    const auto gui_component = utility::re_component::find<REComponent*>(game_object->transform, "via.gui.GUI");

    if (gui_component == nullptr) {
        return;
    }

    static const auto gui_t = sdk::find_type_definition("via.gui.GUI");
    static const auto view_t = sdk::find_type_definition("via.gui.View");

    if (gui_t == nullptr || view_t == nullptr) {
        return;
    }

    static const auto set_res_adjust_scale = view_t->get_method("set_ResAdjustScale(via.gui.ResolutionAdjustScale)");
    static const auto set_res_adjust_anchor = view_t->get_method("set_ResAdjustAnchor(via.gui.ResolutionAdjustAnchor)");
    static const auto set_resolution_adjust = view_t->get_method("set_ResolutionAdjust(System.Boolean)");
    static const auto get_view_type = view_t->get_method("get_ViewType");

    if (set_res_adjust_scale == nullptr || set_res_adjust_anchor == nullptr || set_resolution_adjust == nullptr || get_view_type == nullptr) {
        return;
    }

    static const auto get_view_method = gui_t->get_method("get_View");

    if (get_view_method == nullptr) {
        return;
    }

    const auto view = get_view_method->call<::REManagedObject*>(sdk::get_thread_context(), gui_component);

    if (view == nullptr) {
        return;
    }

    const auto is_screen_view = get_view_type != nullptr && 
                                get_view_type->call<int32_t>(sdk::get_thread_context(), view) == (int32_t)via::gui::ViewType::Screen;

    if (is_screen_view) {
        set_res_adjust_scale->call<void>(sdk::get_thread_context(), view, (int32_t)via::gui::ResolutionAdjustScale::FitSmallRatioAxis);
        set_res_adjust_anchor->call<void>(sdk::get_thread_context(), view, (int32_t)via::gui::ResolutionAdjustAnchor::CenterCenter);
        set_resolution_adjust->call<void>(sdk::get_thread_context(), view, true); // Causes the options to be applied/used
    }
}

bool Graphics::on_pre_gui_draw_element(REComponent* gui_element, void* primitive_context) {
    if (m_disable_gui->value()) {
        return false;
    }

    if (!m_ultrawide_fix->value()) {
        return true;
    }

    // TODO: Check how this interacts with the other games, could be useful for them too.
#if defined(SF6)
    fix_ui_element(gui_element);
#endif

    auto game_object = utility::re_component::get_game_object(gui_element);

    if (game_object != nullptr && game_object->transform != nullptr) {
        const auto name = utility::re_string::get_string(game_object->name);
        const auto name_hash = utility::hash(name);

        switch(name_hash) {
        // RE2/3?
        case "GUI_PillarBox"_fnv:
        case "GUIEventPillar"_fnv:
            game_object->shouldDraw = false;
            return false;

#if defined(DD2)
        case "ui012203"_fnv:
            game_object->shouldDraw = false;
            return false;
#endif

#if defined(RE4)
        case "Gui_ui2510"_fnv: // Black bars in cutscenes
            game_object->shouldDraw = false;
            return false;

        case "AcBackGround"_fnv: // Various screens that show the game background
        case "Gui_ArmouryTab"_fnv: // Typewriter storage
        case "Gui_ui3030"_fnv: // in inventory
        case "Gui_ui3040"_fnv: // just picked up an item
            if (game_object->shouldDraw && game_object->shouldUpdate) {
                std::unique_lock _{m_re4.time_mtx};
                m_re4.last_inventory_open = std::chrono::steady_clock::now();
            }
            break;
#endif

        default:
            break;
        }
    }

    return true;
}

void Graphics::on_view_get_size(REManagedObject* scene_view, float* result) {
#if defined(SF6) || defined(DMC5) || TDB_VER >= 73
    if (m_ultrawide_fix->value()) {
        auto regenny_view = (regenny::via::SceneView*)scene_view;
        auto window = regenny_view->window;

        if (window != nullptr) {
            window->borderless_size.w = (float)window->width;
            window->borderless_size.h = (float)window->height;
        }
    }
#endif

    if (!m_force_render_res_to_window->value() || !m_backbuffer_size.has_value()) {
        return;
    }

    result[0] = (float)(*m_backbuffer_size)[0];
    result[1] = (float)(*m_backbuffer_size)[1];
}

void Graphics::do_scope_tweaks(sdk::renderer::layer::Scene* layer) {
#ifdef RE4
    if (!m_scope_tweaks->value()) {
        return;
    }
    const auto camera = layer->get_camera();

    if (camera == nullptr || !layer->is_enabled()) {
        return;
    }

    const auto camera_gameobject = utility::re_component::get_game_object(camera);

    if (camera_gameobject == nullptr || camera_gameobject->name == nullptr) {
        return;
    }

    const auto name = utility::re_string::get_view(camera_gameobject->name);

    if (name != L"ScopeCamera") {
        return;
    }

    static auto render_output_t = sdk::find_type_definition("via.render.RenderOutput");
    static auto render_output_tt = render_output_t->get_type();

    auto render_output = utility::re_component::find(camera, render_output_tt);

    if (render_output == nullptr) {
        return;
    }

    static auto set_image_quality_method = render_output_t->get_method("set_ImageQuality");
    static auto set_interleave_method = render_output_t->get_method("set_Interleave");

    if (set_image_quality_method != nullptr) {
        set_image_quality_method->call(sdk::get_thread_context(), render_output, m_scope_image_quality->value());
    }

    if (set_interleave_method != nullptr) {
        set_interleave_method->call(sdk::get_thread_context(), render_output, m_scope_interlaced_rendering->value());
    }
#endif
}

void Graphics::on_scene_layer_update(sdk::renderer::layer::Scene* layer, void* render_context) {
#ifdef RE4
    do_scope_tweaks(layer);
#endif
}

void Graphics::do_ultrawide_fix() {
    if (!m_ultrawide_fix->value()) {
        return;
    }

    // No need to perform ultrawide fix if VR is running.
    if (VR::get()->is_hmd_active()) {
        return;
    }

    set_ultrawide_fov(m_ultrawide_vertical_fov->value());

#if defined(RE4)
    {
        std::shared_lock _{m_re4.time_mtx};

        const auto now = std::chrono::steady_clock::now();
        if (now - m_re4.last_inventory_open < std::chrono::milliseconds(100)) {
            return;
        }
    }
#endif

    static auto via_scene_view = sdk::find_type_definition("via.SceneView");
    static auto set_display_type_method = via_scene_view->get_method("set_DisplayType");

    auto main_view = sdk::get_main_view();

    if (main_view == nullptr) {
        return;
    }

    // This disables any kind of pillarboxing and letterboxing.
    // This cannot be directly restored once applied.
    if (set_display_type_method != nullptr) {
        set_display_type_method->call(sdk::get_thread_context(), main_view, via::DisplayType::Fit);
    }
}

void Graphics::do_ultrawide_fov_restore(bool force) {
    if (!m_ultrawide_fix->value() && !force) {
        return;
    }

    // No need to perform ultrawide fix if VR is running.
    if (VR::get()->is_hmd_active()) {
        return;
    }

#if defined(RE4) // Don't restore the FOV if we've just opened the inventory
    const auto now = std::chrono::steady_clock::now();
    if (now - m_re4.last_inventory_open < std::chrono::milliseconds(100)) {
        return;
    }
#endif

    static auto via_camera = sdk::find_type_definition("via.Camera");
    static auto set_fov_method = via_camera->get_method("set_FOV");
    static auto set_vertical_enable_method = via_camera->get_method("set_VerticalEnable");

    std::scoped_lock _{m_fov_mutex};

    if (set_fov_method != nullptr) {
        for (auto it : m_fov_map) {
            auto camera = it.first;
            set_fov_method->call(sdk::get_thread_context(), camera, m_fov_map[camera]);
            utility::re_managed_object::release(camera);
        }
        m_fov_map.clear();
    }

    if (set_vertical_enable_method != nullptr) {
        for (auto it : m_vertical_fov_map) {
            auto camera = it.first;
            set_vertical_enable_method->call(sdk::get_thread_context(), camera, m_vertical_fov_map[camera]);
            utility::re_managed_object::release(camera);
        }
        m_vertical_fov_map.clear();
    }
}

float hor_to_ver_fov(float fov, float aspect_ratio) {
    return glm::degrees(2.f * glm::atan(glm::tan(glm::radians(fov) / 2.f) / aspect_ratio));
}
float ver_to_hor_fov(float fov, float aspect_ratio) {
    return glm::degrees(2.f * glm::atan(glm::tan(glm::radians(fov) / 2.f) * aspect_ratio));
}

void Graphics::set_ultrawide_fov(bool use_vertical_fov) {
    auto camera = sdk::get_primary_camera();

    if (camera == nullptr) {
        return;
    }

    bool allow_changing_fov = true;
#if defined(RE4)
    // Never scale the FOV if the inventory just opened, otherwise it could make the inventory appear much smaller than it should.
    // Unfortunately it doesn't scale right at 21:9 even in the unpatched game.
    const auto now = std::chrono::steady_clock::now();
    if (now - m_re4.last_inventory_open < std::chrono::milliseconds(100)) {
        allow_changing_fov = false;
        use_vertical_fov = false;
        // Clear the cached FOV values as they wouldn't be up to date anymore
        std::scoped_lock _{m_fov_mutex};
        m_fov_map.clear();
    }
#endif

    static auto via_camera = sdk::find_type_definition("via.Camera");
    static auto get_vertical_enable_method = via_camera->get_method("get_VerticalEnable");
    static auto set_vertical_enable_method = via_camera->get_method("set_VerticalEnable");
    static auto get_fov_method = via_camera->get_method("get_FOV");
    static auto set_fov_method = via_camera->get_method("set_FOV");
    static auto get_aspect_method = via_camera->get_method("get_AspectRatio");

    bool was_vertical_fov_enabled = false;
    bool is_vertical_fov_enabled = false;

    if (get_vertical_enable_method != nullptr) {
        was_vertical_fov_enabled = get_vertical_enable_method->call<bool>(sdk::get_thread_context(), camera);

        {
            std::scoped_lock _{m_fov_mutex};

            if (!m_vertical_fov_map.contains(camera)) {
                m_vertical_fov_map[camera] = was_vertical_fov_enabled;
                utility::re_managed_object::add_ref(camera);
            } else {
                m_vertical_fov_map[camera] = was_vertical_fov_enabled;
            }
        }
    }

    if (set_vertical_enable_method != nullptr) {
        set_vertical_enable_method->call(sdk::get_thread_context(), camera, use_vertical_fov);
        is_vertical_fov_enabled = use_vertical_fov;
    }

    if (!allow_changing_fov || get_fov_method == nullptr || set_fov_method == nullptr) {
        return;
    }

    // This is usually the horizontal FOV but it could also be vertical.
    const auto fov = get_fov_method->call<float>(sdk::get_thread_context(), camera);

    {
        std::scoped_lock _{m_fov_mutex};
            
        if (!m_fov_map.contains(camera)) {
            m_fov_map[camera] = fov;
            utility::re_managed_object::add_ref(camera);
        } else {
            m_fov_map[camera] = fov;
        }
    }

    // Customize UW FOV with multiplier
    if (m_ultrawide_custom_fov->value()) {
        // Note: values a certain beyond a certain range might be rejected.
        const auto scaled_fov = std::clamp(fov * m_ultrawide_fov_multiplier->value(), 1.f, 179.f);
        set_fov_method->call(sdk::get_thread_context(), camera, scaled_fov);
    }
    // Automatically patch the FOV to make it look as it does at 16:9 (Hor+ scaling, thus fixed Ver FOV)
    else {
        constexpr float default_aspect_ratio = 16.f / 9.f;
        // The threshold for letter boxing
        constexpr float min_supported_aspect_ratio = default_aspect_ratio;
        // The threshold for pillar boxing (or shifting to Ver- FOV)
#if defined(RE8)
        constexpr float max_supported_aspect_ratio = 32.f / 9.f;
#elif defined(RE2) || defined(RE3) || defined(RE4)
        // Even if most 21:9 resolutions actually have a higher aspect ratio than 2.333, that's actually what some games wrongfully use
        constexpr float max_supported_aspect_ratio = 21.f / 9.f;
#else
        constexpr float max_supported_aspect_ratio = default_aspect_ratio;
#endif

        float current_aspect_ratio = default_aspect_ratio;
        float target_aspect_ratio = default_aspect_ratio;
        // The backbuffer doesn't always represent the game internal aspect ratio, as it also accounts for black bars.
        // For example, when set to borderless and using a game resolution different form the current monitor one, the black
        // bars on the side will be accounted in it, which is why we use it to calculate the target aspect ratio.
        if (m_backbuffer_size.has_value()) {
            const float resolution_x = (float)(*m_backbuffer_size)[0];
            const float resolution_y = (float)(*m_backbuffer_size)[1];
            target_aspect_ratio = resolution_x / resolution_y;
        }
        // The camera aspect ratio represents the aspect ratio the game uses within the black bars
        if (get_aspect_method) {
            current_aspect_ratio = get_aspect_method->call<float>(sdk::get_thread_context(), camera);
        }
        // Depending on the game, the FOV might have already automatically scaled up to "max_supported_aspect_ratio" by the time it reaches here
        const float fov_aspect_ratio = std::clamp(current_aspect_ratio, min_supported_aspect_ratio, max_supported_aspect_ratio);

        if (was_vertical_fov_enabled) {
            // Nothing to do, the FOV should already be correct under any aspect ratio in this case
        }
        else if (target_aspect_ratio >= min_supported_aspect_ratio) {
            const auto vfov = hor_to_ver_fov(fov, fov_aspect_ratio);
            const auto hfov_corrected = ver_to_hor_fov(vfov, target_aspect_ratio);
            set_fov_method->call(sdk::get_thread_context(), camera, is_vertical_fov_enabled ? vfov : hfov_corrected);
        }
        // Account for the letter boxing and expand the vertical aspect ratio
        else {
            const auto vfov = hor_to_ver_fov(fov, target_aspect_ratio);
            const auto hfov_corrected = ver_to_hor_fov(vfov, min_supported_aspect_ratio);
            set_fov_method->call(sdk::get_thread_context(), camera, is_vertical_fov_enabled ? vfov : hfov_corrected);
        }
    }
}

#if TDB_VER >= 69
void Graphics::setup_path_trace_hook() {
    if (m_attempted_path_trace_hook) {
        return;
    }

    m_attempted_path_trace_hook = true;

    spdlog::info("[Graphics] Setting up path trace hook");

    const auto game = utility::get_executable();
    const auto ref = utility::find_function_from_string_ref(game, "RayTraceSettings", true);

    if (!ref.has_value()) {
        spdlog::error("[Graphics] Failed to find function with RayTraceSettings string reference");
        return;
    }

    // gets us the actual function start
    const auto fn = utility::find_function_start_with_call(ref.value());

    if (!fn.has_value()) {
        spdlog::error("[Graphics] Failed to find RayTraceSettings function");
        return;
    }

    spdlog::info("[Graphics] Found RayTraceSettings function @ {:x}", *fn);

    std::unordered_map<size_t, size_t> offset_reference_counts{};

    // Locate RT type offset being referenced within the function
    // We'll need to find the most referenced offset being used with a "cmp" instruction
    utility::exhaustive_decode((uint8_t*)*fn, 1000, [&](utility::ExhaustionContext& ctx) -> utility::ExhaustionResult {
        const auto mnem = std::string_view{ctx.instrux.Mnemonic};
        // Do not care about calls
        if (mnem.starts_with("CALL")) {
            return utility::ExhaustionResult::STEP_OVER;
        }

        if (!mnem.starts_with("CMP")) {
            return utility::ExhaustionResult::CONTINUE;
        }

        // We're looking for cmp dword ptr [reg+offset], imm
        if (ctx.instrux.Operands[0].Type != ND_OP_MEM || ctx.instrux.Operands[1].Type != ND_OP_IMM) {
            return utility::ExhaustionResult::CONTINUE;
        }

        if (!ctx.instrux.Operands[0].Info.Memory.HasBase) {
            return utility::ExhaustionResult::CONTINUE;
        }

        const auto offset = ctx.instrux.Operands[0].Info.Memory.Disp;

        // ones that are comparing to 6 (ASVGF) are the right one.
        if (ctx.instrux.Operands[0].Info.Memory.DispSize != 4 || (ctx.instrux.Operands[1].Info.Immediate.Imm & 0xFFFFFFFF) != 6) {
            return utility::ExhaustionResult::CONTINUE;
        }

        if (offset_reference_counts.contains(offset)) {
            offset_reference_counts[offset]++;
        } else {
            offset_reference_counts[offset] = 1;
        }

        spdlog::info("[Graphics] Encountered a CMP offset @ {:x}", offset);

        return utility::ExhaustionResult::CONTINUE;
    });

    if (offset_reference_counts.empty()) {
        spdlog::error("[Graphics] Failed to find any RT type offsets");
        return;
    }

    const auto max_offset = std::max_element(offset_reference_counts.begin(), offset_reference_counts.end(), [](const auto& a, const auto& b) {
        return a.second < b.second;
    });

    if (max_offset == offset_reference_counts.end()) {
        spdlog::error("[Graphics] Failed to find most referenced RT type offset");
        return;
    }

    m_rt_type_offset = max_offset->first;
    spdlog::info("[Graphics] Found RT type offset @ {:x}", *m_rt_type_offset);

    m_rt_draw_impl_hook = std::make_unique<FunctionHook>(*fn, (uintptr_t)rt_draw_impl_hook);
    if (!m_rt_draw_impl_hook->create()) {
        spdlog::error("[Graphics] Failed to create path trace draw impl hook");
        return;
    }

    const auto draw_ref = utility::find_function_from_string_ref(game, "Bounce2", true);

    if (!draw_ref.has_value()) {
        spdlog::error("[Graphics] Failed to find function with Bounce2 string reference");
        return;
    }

    const auto draw_fn = utility::find_virtual_function_start(draw_ref.value());

    if (!draw_fn.has_value()) {
        spdlog::error("[Graphics] Failed to find Bounce2 function");
        return;
    }

    m_rt_draw_hook = std::make_unique<FunctionHook>(*draw_fn, (uintptr_t)rt_draw_hook);

    if (!m_rt_draw_hook->create()) {
        spdlog::error("[Graphics] Failed to create path trace draw hook");
        return;
    }

    spdlog::info("[Graphics] Path trace hook set up");
}

void Graphics::setup_rt_component() {
    static const auto rt_t = sdk::find_type_definition("via.render.ExperimentalRayTrace");

    if (rt_t == nullptr) {
        return;
    }
    
    const auto camera = sdk::get_primary_camera();

    if (camera == nullptr) {
        return;
    }

    const auto game_object = utility::re_component::get_game_object(camera);

    if (game_object == nullptr || game_object->transform == nullptr) {
        return;
    }

    const auto go_name = utility::re_string::get_view(game_object->name);

    if ((!go_name.starts_with(L"Main") && !go_name.starts_with(L"main")) && !go_name.contains(L"DefaultCamera")) {
        return;
    }

    auto rt_component = utility::re_component::find<REComponent>(game_object->transform, rt_t->get_type());
    
    // Attempt to create the component if it doesn't exist
    if (rt_component == nullptr) {
        rt_component = sdk::call_object_func_easy<REComponent*>(game_object, "createComponent(System.Type)", rt_t->get_runtime_type());

        if (rt_component != nullptr) {
            spdlog::info("[Graphics] Successfully created new RT component @ {:x}", (uintptr_t)rt_component);
        }
    }

    if (rt_component == nullptr) {
        m_rt_component.reset();
        return;
    }

    if (m_rt_component.get() != (sdk::ManagedObject*)rt_component) {
        m_rt_component = (sdk::ManagedObject*)rt_component;
    }

    if (m_rt_cloned_component.get() == nullptr && m_ray_trace_clone_type_true->value() > (int32_t)RayTraceType::Disabled) {
        m_rt_cloned_component = (sdk::ManagedObject*)rt_t->create_instance_full(false);

        if (m_rt_cloned_component.get() != nullptr) {
            spdlog::info("[Graphics] Successfully cloned RT component @ {:x}", (uintptr_t)m_rt_cloned_component.get());
        }
    }
}

void Graphics::apply_ray_tracing_tweaks() {
    setup_rt_component();

    if (m_rt_component == nullptr) {
        return;
    }

    static const auto rt_t = sdk::find_type_definition("via.render.ExperimentalRayTrace");

    if (rt_t == nullptr) {
        return;
    }

    static const auto set_RaytracingMode = rt_t->get_method("set_RaytracingMode");
    static const auto setBounce = rt_t->get_method("setBounce");
    static const auto setSpp = rt_t->get_method("setSpp");

    auto fix = [this](sdk::ManagedObject* target, int32_t rt_type) {
        if (target == nullptr) {
            return;
        }

        const auto context = sdk::get_thread_context();

        if (set_RaytracingMode != nullptr && rt_type > (int32_t)RayTraceType::Disabled) {
            set_RaytracingMode->call<void>(context, target, rt_type - 1);
        }

        if (rt_type == (int32_t)RayTraceType::Hybrid || rt_type == (int32_t)RayTraceType::Pure) {
            if (setBounce != nullptr) {
                setBounce->call<void>(context, target, m_bounce_count->value());
            }

            if (setSpp != nullptr) {
                setSpp->call<void>(context, target, m_samples_per_pixel->value());
            }
        }
    };

    fix(m_rt_component.get(), m_ray_trace_type->value());
    fix(m_rt_cloned_component.get(), m_ray_trace_clone_type_true->value());
}

void* Graphics::rt_draw_hook(REComponent* rt, void* draw_context, void* r8, void* r9) {
    auto& graphics = Graphics::get();

    const auto og = graphics->m_rt_draw_hook->get_original<decltype(rt_draw_hook)>();
    const auto result = og(rt, draw_context, r8, r9);
    
    if (graphics->m_rt_cloned_component.get() == nullptr) {
        return result;
    }

    if (graphics->m_ray_tracing_tweaks->value() && graphics->m_ray_trace_clone_type_true->value() > (int32_t)RayTraceType::Disabled) {
        auto go = utility::re_component::get_game_object(rt);

        if (go == nullptr || go->transform == nullptr) {
            return result;
        }

        static auto rt_t = sdk::find_type_definition("via.render.ExperimentalRayTrace");

        if (rt_t == nullptr) {
            return result;
        }

        auto replaceable_rt = utility::re_component::find_replaceable<REComponent>(go->transform, rt_t->get_type());

        // The cursed part of the code
        if (replaceable_rt != nullptr) {
            *replaceable_rt = (REComponent*)graphics->m_rt_cloned_component.get();
            og((REComponent*)graphics->m_rt_cloned_component.get(), draw_context, r8, r9);
            *replaceable_rt = rt;
        }
    }

    return result;
}

void* Graphics::rt_draw_impl_hook(void* rt_impl, void* draw_context, void* r8, void* r9, void* unk) {
    auto& graphics = Graphics::get();

    uint8_t& ray_tracing_mode = *(uint8_t*)((uintptr_t)rt_impl + graphics->m_rt_type_offset.value());
    const auto old_mode = ray_tracing_mode;
    const auto og = graphics->m_rt_draw_impl_hook->get_original<decltype(rt_draw_impl_hook)>();

    if (graphics->m_ray_tracing_tweaks->value() && graphics->m_ray_trace_clone_type_pre->value() > (int32_t)RayTraceType::Disabled) {
        ray_tracing_mode = graphics->m_ray_trace_clone_type_pre->value() - 1;
        og(rt_impl, draw_context, r8, r9, unk);
        ray_tracing_mode = old_mode;
    }

    const auto result = og(rt_impl, draw_context, r8, r9, unk);

    if (graphics->m_ray_tracing_tweaks->value() && graphics->m_ray_trace_clone_type_post->value() > (int32_t)RayTraceType::Disabled) {
        ray_tracing_mode = graphics->m_ray_trace_clone_type_post->value() - 1;
        og(rt_impl, draw_context, r8, r9, unk);
        ray_tracing_mode = old_mode;
    }

    return result;
}
#endif