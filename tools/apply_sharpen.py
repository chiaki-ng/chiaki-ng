#!/usr/bin/env python3
"""Adds a 'Sharpening' option (Off/Low/Medium/High) to chiaki-ng.
Usage: python3 apply_sharpen.py <repo-root>
Idempotent: does nothing if already applied."""
import sys, pathlib

root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".")

def patch(rel, edits):
    p = root / rel
    s = p.read_text(encoding="utf-8")
    if "sharpenLevel" in s or "GetSharpenLevel" in s or "sharpen_hooks" in s or "CAS.glsl" in s:
        print(f"skip (already patched): {rel}")
        return
    for old, new in edits:
        n = s.count(old)
        if n != 1:
            sys.exit(f"ERROR {rel}: anchor found {n} times (expected 1):\n{old}")
        s = s.replace(old, new)
    p.write_text(s, encoding="utf-8")
    print(f"patched: {rel}")

# --- settings storage
patch("gui/include/settings.h", [(
"\t\tvoid SetVulkanDeferredSwap(bool enabled) { settings.setValue(\"settings/vulkan_deferred_swap\", enabled); }\n",
"\t\tvoid SetVulkanDeferredSwap(bool enabled) { settings.setValue(\"settings/vulkan_deferred_swap\", enabled); }\n"
"\t\t// 0 = off, 1 = low, 2 = medium, 3 = high\n"
"\t\tint GetSharpenLevel() const { return qBound(0, settings.value(\"settings/sharpen_level\", 0).toInt(), 3); }\n"
"\t\tvoid SetSharpenLevel(int level) { settings.setValue(\"settings/sharpen_level\", qBound(0, level, 3)); }\n")])

# --- QML bridge
patch("gui/include/qmlsettings.h", [
("    Q_PROPERTY(bool vulkanDeferredSwap READ vulkanDeferredSwap WRITE setVulkanDeferredSwap NOTIFY vulkanDeferredSwapChanged)\n",
 "    Q_PROPERTY(bool vulkanDeferredSwap READ vulkanDeferredSwap WRITE setVulkanDeferredSwap NOTIFY vulkanDeferredSwapChanged)\n"
 "    Q_PROPERTY(int sharpenLevel READ sharpenLevel WRITE setSharpenLevel NOTIFY sharpenLevelChanged)\n"),
("    void setVulkanDeferredSwap(bool enabled);\n",
 "    void setVulkanDeferredSwap(bool enabled);\n    int sharpenLevel() const;\n    void setSharpenLevel(int level);\n"),
("    void vulkanDeferredSwapChanged();\n",
 "    void vulkanDeferredSwapChanged();\n    void sharpenLevelChanged();\n"),
])

patch("gui/src/qmlsettings.cpp", [
("int QmlSettings::windowType() const\n",
 "int QmlSettings::sharpenLevel() const\n{\n    return settings->GetSharpenLevel();\n}\n\n"
 "void QmlSettings::setSharpenLevel(int level)\n{\n    settings->SetSharpenLevel(level);\n    emit sharpenLevelChanged();\n}\n\n"
 "int QmlSettings::windowType() const\n"),
("    emit vulkanDeferredSwapChanged();\n    emit autoConnectMacChanged();\n",
 "    emit vulkanDeferredSwapChanged();\n    emit sharpenLevelChanged();\n    emit autoConnectMacChanged();\n"),
])

# --- renderer
patch("gui/include/qmlmainwindow.h", [(
"    const struct pl_hook *fsrcnnx_hook_16 = nullptr;\n",
"    const struct pl_hook *fsrcnnx_hook_16 = nullptr;\n    const struct pl_hook *sharpen_hooks[3] = {nullptr, nullptr, nullptr};\n")])

patch("gui/src/qmlmainwindow.cpp", [
("static bool uses_fsrcnnx(PlaceboUpscaler upscaler)\n",
 "// Loads the contrast-adaptive sharpening shader with a fixed strength baked in.\n"
 "static const struct pl_hook *load_sharpen_hook(pl_gpu gpu, float strength)\n{\n"
 "    QFile file(QStringLiteral(\":/shaders/CAS.glsl\"));\n"
 "    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {\n"
 "        qCWarning(chiakiGui) << \"Failed to open sharpening shader resource\";\n"
 "        return nullptr;\n    }\n\n"
 "    QByteArray shader = file.readAll();\n"
 "    shader.replace(\"@STRENGTH@\", QByteArray::number(static_cast<double>(strength), 'f', 2));\n"
 "    const struct pl_hook *hook = pl_mpv_user_shader_parse(gpu, shader.constData(), static_cast<size_t>(shader.size()));\n"
 "    if (!hook)\n        qCWarning(chiakiGui) << \"Failed to parse sharpening shader\";\n"
 "    return hook;\n}\n\n"
 "static bool uses_fsrcnnx(PlaceboUpscaler upscaler)\n"),
("    this->fsrcnnx_hook_16 = load_mpv_hook(placeboGpu(), QStringLiteral(\":/shaders/FSRCNNX_x2_16-0-4-1.glsl\"));\n",
 "    this->fsrcnnx_hook_16 = load_mpv_hook(placeboGpu(), QStringLiteral(\":/shaders/FSRCNNX_x2_16-0-4-1.glsl\"));\n"
 "    this->sharpen_hooks[0] = load_sharpen_hook(placeboGpu(), 0.35f);\n"
 "    this->sharpen_hooks[1] = load_sharpen_hook(placeboGpu(), 0.60f);\n"
 "    this->sharpen_hooks[2] = load_sharpen_hook(placeboGpu(), 0.85f);\n"),
("    pl_mpv_user_shader_destroy(&fsrcnnx_hook_16);\n",
 "    pl_mpv_user_shader_destroy(&fsrcnnx_hook_16);\n"
 "    for (auto &sharpen_hook : sharpen_hooks)\n        pl_mpv_user_shader_destroy(&sharpen_hook);\n"),
("    for (int i = 0; i < frame_mix.num_frames; i++) {\n        const struct pl_frame *mix_frame = frame_mix.frames[i];\n",
 "    // Optional contrast-adaptive sharpening, applied after scaling (hook stage OUTPUT).\n"
 "    const int sharpen_level = settings ? settings->GetSharpenLevel() : 0;\n"
 "    const struct pl_hook *sharpen_hook = (sharpen_level >= 1 && sharpen_level <= 3)\n"
 "        ? sharpen_hooks[sharpen_level - 1] : nullptr;\n"
 "    const struct pl_hook *all_hooks[2] = {nullptr, nullptr};\n"
 "    int num_all_hooks = 0;\n"
 "    if (fsrcnnx_hook)\n        all_hooks[num_all_hooks++] = fsrcnnx_hook;\n"
 "    if (sharpen_hook)\n        all_hooks[num_all_hooks++] = sharpen_hook;\n"
 "    if (num_all_hooks > 0) {\n        params.hooks = all_hooks;\n        params.num_hooks = num_all_hooks;\n    }\n\n"
 "    for (int i = 0; i < frame_mix.num_frames; i++) {\n        const struct pl_frame *mix_frame = frame_mix.frames[i];\n"),
])

patch("gui/res/resources.qrc", [(
"    <file alias=\"FSR.glsl\">shaders/FSR.glsl</file>\n",
"    <file alias=\"FSR.glsl\">shaders/FSR.glsl</file>\n    <file alias=\"CAS.glsl\">shaders/CAS.glsl</file>\n")])

patch("gui/src/qml/SettingsDialog.qml", [(
"""                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Renderer Backend:")
                    }
""",
"""                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Sharpening:")
                    }

                    C.ComboBox {
                        Layout.preferredWidth: 400
                        model: [qsTr("Off"), qsTr("Low"), qsTr("Medium"), qsTr("High")]
                        currentIndex: Chiaki.settings.sharpenLevel
                        onActivated: index => Chiaki.settings.sharpenLevel = index
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(Off)")
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Renderer Backend:")
                    }
""")])
