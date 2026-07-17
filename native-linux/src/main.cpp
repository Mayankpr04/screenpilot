#include <gtk/gtk.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <unistd.h>

namespace fs = std::filesystem;

enum class Control { Brightness, Contrast, BlackLevel };

struct RangeValue {
    int minimum{0};
    int current{0};
    int maximum{100};
};

class Backend {
public:
    virtual ~Backend() = default;
    virtual std::optional<RangeValue> read(Control control) = 0;
    virtual bool write(Control control, int value) = 0;
    virtual std::string errorMessage() const { return "The display rejected this setting."; }
};

std::optional<std::string> run(const std::vector<std::string>& args) {
    std::vector<gchar*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& arg : args) argv.push_back(const_cast<gchar*>(arg.c_str()));
    argv.push_back(nullptr);
    gchar* output = nullptr;
    gchar* error = nullptr;
    gint status = 0;
    GError* spawnError = nullptr;
    const gboolean ok = g_spawn_sync(nullptr, argv.data(), nullptr, G_SPAWN_SEARCH_PATH,
                                     nullptr, nullptr, &output, &error, &status, &spawnError);
    std::string result = output ? output : "";
    g_free(output);
    g_free(error);
    if (spawnError) g_error_free(spawnError);
    if (!ok || !g_spawn_check_wait_status(status, nullptr)) return std::nullopt;
    return result;
}

class BacklightBackend final : public Backend {
public:
    explicit BacklightBackend(fs::path path) : path_(std::move(path)) {}

    std::optional<RangeValue> read(Control control) override {
        if (control != Control::Brightness) return std::nullopt;
        const auto current = readNumber(path_ / "brightness");
        const auto maximum = readNumber(path_ / "max_brightness");
        if (!current || !maximum || *maximum <= 0) return std::nullopt;
        return RangeValue{0, static_cast<int>(*current * 100 / *maximum), 100};
    }

    bool write(Control control, int value) override {
        if (control != Control::Brightness) return false;
        const auto maximum = readNumber(path_ / "max_brightness");
        if (!maximum) return false;
        const long raw = std::clamp(value, 0, 100) * *maximum / 100;
        std::ofstream stream(path_ / "brightness");
        if (stream) {
            stream << raw;
            if (stream.good()) return true;
        }
        return run({"brightnessctl", "--device", path_.filename().string(), "set",
                    std::to_string(std::clamp(value, 0, 100)) + "%"}).has_value();
    }

    std::string errorMessage() const override {
        const fs::path brightness = path_ / "brightness";
        if (access(brightness.c_str(), W_OK) != 0)
            return "ScreenPilot does not have permission to change the laptop backlight. "
                   "Sign out and back in (or restart) after installation, then try again.";
        return "The laptop firmware rejected this brightness setting.";
    }

private:
    static std::optional<long> readNumber(const fs::path& path) {
        std::ifstream stream(path);
        long value = 0;
        if (!(stream >> value)) return std::nullopt;
        return value;
    }
    fs::path path_;
};

class DdcBackend final : public Backend {
public:
    explicit DdcBackend(int bus) : bus_(bus) {}

    std::optional<RangeValue> read(Control control) override {
        const auto output = run({"ddcutil", "--bus", std::to_string(bus_), "getvcp",
                                 code(control), "--terse"});
        if (!output) return std::nullopt;
        std::smatch match;
        if (!std::regex_search(*output, match,
                std::regex(R"(VCP\s+[0-9A-Fa-f]+\s+C\s+(\d+)\s+(\d+))"))) return std::nullopt;
        const int current = std::stoi(match[1]);
        const int maximum = std::stoi(match[2]);
        if (maximum <= 0) return std::nullopt;
        return RangeValue{0, current, maximum};
    }

    bool write(Control control, int value) override {
        return run({"ddcutil", "--bus", std::to_string(bus_), "setvcp", code(control),
                    std::to_string(value)}).has_value();
    }

    std::string errorMessage() const override {
        const fs::path device = "/dev/i2c-" + std::to_string(bus_);
        if (access(device.c_str(), R_OK | W_OK) != 0)
            return "ScreenPilot cannot access this monitor's I2C device. "
                   "Sign out and back in (or restart) after installation, then try again.";
        return "The monitor rejected this DDC/CI setting. Check that DDC/CI is enabled "
               "in the monitor's on-screen menu.";
    }

private:
    static std::string code(Control control) {
        if (control == Control::Brightness) return "10";
        if (control == Control::Contrast) return "12";
        return "11";
    }
    int bus_;
};

struct Display {
    std::string name;
    std::string type;
    std::unique_ptr<Backend> backend;
    std::unordered_map<Control, RangeValue> controls;
};

void probe(Display& display) {
    for (Control control : {Control::Brightness, Control::Contrast, Control::BlackLevel}) {
        if (auto value = display.backend->read(control)) display.controls[control] = *value;
    }
}

std::vector<Display> discover() {
    std::vector<Display> displays;
    const fs::path root("/sys/class/backlight");
    std::error_code ec;
    if (fs::exists(root, ec)) {
        for (const auto& entry : fs::directory_iterator(root, ec)) {
            Display display{"Built-in display", "Laptop panel",
                            std::make_unique<BacklightBackend>(entry.path()), {}};
            probe(display);
            if (!display.controls.empty()) displays.push_back(std::move(display));
        }
    }

    const auto detected = run({"ddcutil", "detect", "--terse"});
    if (!detected) return displays;
    std::istringstream lines(*detected);
    std::string line;
    int bus = -1;
    std::string name;
    auto finish = [&] {
        if (bus < 0) return;
        Display display{name.empty() ? "External monitor" : name, "External / DDC-CI",
                        std::make_unique<DdcBackend>(bus), {}};
        probe(display);
        if (!display.controls.empty()) displays.push_back(std::move(display));
        bus = -1;
        name.clear();
    };
    while (std::getline(lines, line)) {
        if (line.rfind("Display ", 0) == 0) finish();
        std::smatch match;
        if (std::regex_search(line, match, std::regex(R"(/dev/i2c-(\d+))")))
            bus = std::stoi(match[1]);
        const auto monitor = line.find("Monitor:");
        if (monitor != std::string::npos) {
            name = line.substr(monitor + 8);
            name.erase(0, name.find_first_not_of(" \t"));
        }
    }
    finish();
    return displays;
}

const char* label(Control control) {
    if (control == Control::Brightness) return "Brightness";
    if (control == Control::Contrast) return "Contrast";
    return "Black level";
}

struct Binding {
    Display* display;
    Control control;
    GtkWidget* valueLabel;
    guint timer{0};
    int value{0};
};

gboolean applyValue(gpointer data) {
    auto* binding = static_cast<Binding*>(data);
    binding->timer = 0;
    if (!binding->display->backend->write(binding->control, binding->value)) {
        GtkWidget* dialog = gtk_message_dialog_new(nullptr, GTK_DIALOG_MODAL,
            GTK_MESSAGE_WARNING, GTK_BUTTONS_CLOSE, "%s",
            binding->display->backend->errorMessage().c_str());
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
    return G_SOURCE_REMOVE;
}

void changed(GtkRange* range, gpointer data) {
    auto* binding = static_cast<Binding*>(data);
    binding->value = static_cast<int>(gtk_range_get_value(range));
    const std::string text = std::to_string(binding->value);
    gtk_label_set_text(GTK_LABEL(binding->valueLabel), text.c_str());
    if (binding->timer) g_source_remove(binding->timer);
    binding->timer = g_timeout_add(140, applyValue, binding);
}

void destroyBinding(gpointer data, GClosure*) {
    auto* binding = static_cast<Binding*>(data);
    if (binding->timer) g_source_remove(binding->timer);
    delete binding;
}

std::vector<Display> displays;
GtkWidget* listBox = nullptr;

void rebuild() {
    GList* children = gtk_container_get_children(GTK_CONTAINER(listBox));
    for (GList* item = children; item; item = item->next)
        gtk_widget_destroy(GTK_WIDGET(item->data));
    g_list_free(children);
    displays = discover();
    if (displays.empty()) {
        GtkWidget* empty = gtk_label_new("No controllable displays found. Check DDC/CI and device permissions.");
        gtk_box_pack_start(GTK_BOX(listBox), empty, FALSE, FALSE, 12);
    }
    for (auto& display : displays) {
        GtkWidget* frame = gtk_frame_new(nullptr);
        gtk_style_context_add_class(gtk_widget_get_style_context(frame), "card");
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        gtk_container_add(GTK_CONTAINER(frame), box);
        GtkWidget* title = gtk_label_new(display.name.c_str());
        gtk_widget_set_halign(title, GTK_ALIGN_START);
        gtk_style_context_add_class(gtk_widget_get_style_context(title), "title");
        gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 0);
        GtkWidget* type = gtk_label_new(display.type.c_str());
        gtk_widget_set_halign(type, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(box), type, FALSE, FALSE, 0);
        for (Control control : {Control::Brightness, Control::Contrast, Control::BlackLevel}) {
            auto found = display.controls.find(control);
            if (found == display.controls.end()) continue;
            GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
            GtkWidget* name = gtk_label_new(label(control));
            gtk_widget_set_size_request(name, 90, -1);
            gtk_widget_set_halign(name, GTK_ALIGN_START);
            GtkWidget* scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                found->second.minimum, found->second.maximum, 1);
            gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
            gtk_range_set_value(GTK_RANGE(scale), found->second.current);
            gtk_widget_set_hexpand(scale, TRUE);
            GtkWidget* value = gtk_label_new(std::to_string(found->second.current).c_str());
            gtk_widget_set_size_request(value, 38, -1);
            gtk_box_pack_start(GTK_BOX(row), name, FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(row), scale, TRUE, TRUE, 0);
            gtk_box_pack_start(GTK_BOX(row), value, FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
            auto* binding = new Binding{&display, control, value};
            g_signal_connect_data(scale, "value-changed", G_CALLBACK(changed), binding,
                                  destroyBinding, static_cast<GConnectFlags>(0));
        }
        gtk_box_pack_start(GTK_BOX(listBox), frame, FALSE, FALSE, 8);
    }
    gtk_widget_show_all(listBox);
}

void refreshClicked(GtkButton*, gpointer) { rebuild(); }

int main(int argc, char** argv) {
    gtk_init(&argc, &argv);
    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        "window{background:#121722;color:#dce4f2} label{color:#dce4f2}"
        ".card{background:#202838;border:1px solid #344057;border-radius:10px;padding:14px}"
        ".title{font-size:18px;font-weight:600}", -1, nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "ScreenPilot");
    gtk_window_set_default_size(GTK_WINDOW(window), 680, 520);
    gtk_window_set_icon_name(GTK_WINDOW(window), "screenpilot");
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
    GtkWidget* outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 18);
    gtk_container_add(GTK_CONTAINER(window), outer);
    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* heading = gtk_label_new("ScreenPilot");
    gtk_widget_set_halign(heading, GTK_ALIGN_START);
    PangoAttrList* attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_scale_new(1.8));
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes(GTK_LABEL(heading), attrs);
    pango_attr_list_unref(attrs);
    GtkWidget* refresh = gtk_button_new_with_label("Refresh displays");
    g_signal_connect(refresh, "clicked", G_CALLBACK(refreshClicked), nullptr);
    gtk_box_pack_start(GTK_BOX(header), heading, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(header), refresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), header, FALSE, FALSE, 0);
    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    listBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_add(GTK_CONTAINER(scroll), listBox);
    gtk_box_pack_start(GTK_BOX(outer), scroll, TRUE, TRUE, 0);
    rebuild();
    gtk_widget_show_all(window);
    gtk_main();
    g_object_unref(css);
    return 0;
}

