#include "qmlsettings.h"
#include "sessionlog.h"
#include <controllermanager.h>
#include <gyrosteer.h>

#include <QSet>
#include <QKeySequence>
#include <QFutureWatcher>
#include <QtConcurrentRun>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QFileDialog>
#include <QApplication>
#include <QCoreApplication>
#include <QProcess>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <SDL.h>
}

static bool codecSupportsHwDevice(enum AVCodecID codec_id, AVHWDeviceType type)
{
    const AVCodec *codec = avcodec_find_decoder(codec_id);
    if (!codec)
        return false;

    for (int i = 0;; i++) {
        const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i);
        if (!config)
            return false;
        if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) && config->device_type == type)
            return true;
    }
}

static bool hwDecoderRuntimeAvailable(const QString &name)
{
    const QByteArray name_utf8 = name.toUtf8();
    const AVHWDeviceType type = av_hwdevice_find_type_by_name(name_utf8.constData());
    if (type == AV_HWDEVICE_TYPE_NONE)
        return false;

    // CUDA can be compiled into FFmpeg even when the runtime driver/device is unusable,
    // or the h264/hevc decoders were built without a CUDA hwconfig.
    if (type == AV_HWDEVICE_TYPE_CUDA) {
        AVBufferRef *hw_device_ctx = nullptr;
        const int err = av_hwdevice_ctx_create(&hw_device_ctx, type, nullptr, nullptr, 0);
        if (hw_device_ctx)
            av_buffer_unref(&hw_device_ctx);
        if (err < 0)
            return false;
        return codecSupportsHwDevice(AV_CODEC_ID_H264, type)
            && codecSupportsHwDevice(AV_CODEC_ID_HEVC, type);
    }

    return true;
}

QmlSettings::QmlSettings(Settings *settings, QObject *parent)
    : QObject(parent)
    , settings(settings)
{
    if (settings->GetLogVerbose())
        QLoggingCategory::setFilterRules(QStringLiteral("chiaki.gui.debug=true"));

    connect(settings, &Settings::RegisteredHostsUpdated, this, &QmlSettings::registeredHostsChanged);
    connect(settings, &Settings::ProfilesUpdated, this, &QmlSettings::profilesChanged);
}

bool QmlSettings::remotePlayAsk() const
{
    return settings->GetRemotePlayAsk();
}

void QmlSettings::setRemotePlayAsk(bool asked)
{
    settings->SetRemotePlayAsk(asked);
    emit remotePlayAskChanged();
}

bool QmlSettings::addSteamShortcutAsk() const
{
    return settings->GetAddSteamShortcutAsk();
}

void QmlSettings::setAddSteamShortcutAsk(bool asked)
{
    settings->SetAddSteamShortcutAsk(asked);
    emit addSteamShortcutAskChanged();
}

bool QmlSettings::hideCursor() const
{
    return settings->GetHideCursor();
}

void QmlSettings::setHideCursor(bool enabled)
{
    settings->SetHideCursor(enabled);
    emit hideCursorChanged();
}

bool QmlSettings::showStreamStats() const
{
    return settings->GetShowStreamStats();
}

void QmlSettings::setShowStreamStats(bool enabled)
{
    settings->SetShowStreamStats(enabled);
    emit showStreamStatsChanged();
}

bool QmlSettings::streamerMode() const
{
    return settings->GetStreamerMode();
}

void QmlSettings::setStreamerMode(bool enabled)
{
    settings->SetStreamerMode(enabled);
    emit streamerModeChanged();
}

float QmlSettings::hapticOverride() const
{
    return settings->GetHapticOverride();
}

void QmlSettings::setHapticOverride(float override)
{
    settings->SetHapticOverride(override);
    emit hapticOverrideChanged();
}

bool QmlSettings::ds5GyroFix() const
{
    return settings->GetDS5GyroFixEnabled();
}

void QmlSettings::setDS5GyroFix(bool enabled)
{
    settings->SetDS5GyroFixEnabled(enabled);
    emit ds5GyroFixChanged();
}

bool QmlSettings::hapticsAntiLatency() const
{
    return settings->GetHapticsAntiLatencyEnabled();
}

void QmlSettings::setHapticsAntiLatency(bool enabled)
{
    settings->SetHapticsAntiLatencyEnabled(enabled);
    emit hapticsAntiLatencyChanged();
}

int QmlSettings::hapticsAntiLatencyMs() const
{
    return settings->GetHapticsAntiLatencyMs();
}

void QmlSettings::setHapticsAntiLatencyMs(int ms)
{
    settings->SetHapticsAntiLatencyMs(ms);
    emit hapticsAntiLatencyMsChanged();
}

int QmlSettings::audioVideoDisabled() const
{
    return static_cast<int>(settings->GetAudioVideoDisabled());
}

void QmlSettings::setAudioVideoDisabled(int disabled)
{
    settings->SetAudioVideoDisabled(static_cast<ChiakiDisableAudioVideo>(disabled));
    emit audioVideoDisabledChanged();
}

int QmlSettings::resolutionLocalPS4() const
{
    return settings->GetResolutionLocalPS4();
}

void QmlSettings::setResolutionLocalPS4(int resolution)
{
    settings->SetResolutionLocalPS4(static_cast<ChiakiVideoResolutionPreset>(resolution));
    emit resolutionLocalPS4Changed();
}

int QmlSettings::resolutionRemotePS4() const
{
    return settings->GetResolutionRemotePS4();
}

void QmlSettings::setResolutionRemotePS4(int resolution)
{
    settings->SetResolutionRemotePS4(static_cast<ChiakiVideoResolutionPreset>(resolution));
    emit resolutionRemotePS4Changed();
}

int QmlSettings::resolutionLocalPS5() const
{
    return settings->GetResolutionLocalPS5();
}

void QmlSettings::setResolutionLocalPS5(int resolution)
{
    settings->SetResolutionLocalPS5(static_cast<ChiakiVideoResolutionPreset>(resolution));
    emit resolutionLocalPS5Changed();
}

int QmlSettings::resolutionRemotePS5() const
{
    return settings->GetResolutionRemotePS5();
}

void QmlSettings::setResolutionRemotePS5(int resolution)
{
    settings->SetResolutionRemotePS5(static_cast<ChiakiVideoResolutionPreset>(resolution));
    emit resolutionRemotePS5Changed();
}

int QmlSettings::disconnectAction() const
{
    return static_cast<int>(settings->GetDisconnectAction());
}

void QmlSettings::setDisconnectAction(int action)
{
    settings->SetDisconnectAction(static_cast<DisconnectAction>(action));
    emit disconnectActionChanged();
}

int QmlSettings::suspendAction() const
{
    return static_cast<int>(settings->GetSuspendAction());
}

void QmlSettings::setSuspendAction(int action)
{
    settings->SetSuspendAction(static_cast<SuspendAction>(action));
    emit suspendActionChanged();
}

bool QmlSettings::logVerbose() const
{
    return settings->GetLogVerbose();
}

void QmlSettings::setLogVerbose(bool verbose)
{
    settings->SetLogVerbose(verbose);
    QLoggingCategory::setFilterRules(QStringLiteral("chiaki.gui.debug=%1").arg(verbose ? "true" : "false"));
    emit logVerboseChanged();
}

bool QmlSettings::logSanitize() const
{
    return settings->GetLogSanitize();
}

void QmlSettings::setLogSanitize(bool enabled)
{
    settings->SetLogSanitize(enabled);
    emit logSanitizeChanged();
}

bool QmlSettings::vSyncEnabled() const
{
    return settings->GetVSyncEnabled();
}

void QmlSettings::setVSyncEnabled(bool enabled)
{
    settings->SetVSyncEnabled(enabled);
    emit vSyncEnabledChanged();
}

int QmlSettings::rumbleHapticsIntensity() const
{
    return static_cast<int>(settings->GetRumbleHapticsIntensity());
}

void QmlSettings::setRumbleHapticsIntensity(int intensity)
{
    settings->SetRumbleHapticsIntensity(static_cast<RumbleHapticsIntensity>(intensity));
    emit rumbleHapticsIntensityChanged();
}

bool QmlSettings::iDROnFECFailureEnabled() const
{
    return settings->GetIDROnFECFailureEnabled();
}

void QmlSettings::setIDROnFECFailureEnabled(bool enabled)
{
    settings->SetIDROnFECFailureEnabled(enabled);
    emit iDROnFECFailureEnabledChanged();
}

#ifdef CHIAKI_GUI_ENABLE_STEAMDECK_NATIVE
bool QmlSettings::steamDeckHaptics() const
{
    return settings->GetSteamDeckHapticsEnabled();
}

void QmlSettings::setSteamDeckHaptics(bool steamDeckHaptics)
{
    settings->SetSteamDeckHapticsEnabled(steamDeckHaptics);
    emit steamDeckHapticsChanged();
}

bool QmlSettings::verticalDeck() const
{
    return settings->GetVerticalDeckEnabled();
}

void QmlSettings::setVerticalDeck(bool vertical)
{
    settings->SetVerticalDeckEnabled(vertical);
    emit verticalDeckChanged();
}
#endif

bool QmlSettings::fullscreenDoubleClick() const
{
    return settings->GetFullscreenDoubleClickEnabled();
}

void QmlSettings::setFullscreenDoubleClick(bool enabled)
{
    settings->SetFullscreenDoubleClickEnabled(enabled);
    emit fullscreenDoubleClickChanged();
}

bool QmlSettings::buttonsByPosition() const
{
    return settings->GetButtonsByPosition();
}

void QmlSettings::setButtonsByPosition(bool buttonsByPosition)
{
    settings->SetButtonsByPosition(buttonsByPosition);
    emit buttonsByPositionChanged();
}

bool QmlSettings::startMicUnmuted() const
{
    return settings->GetStartMicUnmuted();
}

void QmlSettings::setStartMicUnmuted(bool startMicUnmuted)
{
    settings->SetStartMicUnmuted(startMicUnmuted);
    emit startMicUnmutedChanged();
}

#if CHIAKI_GUI_ENABLE_SPEEX
bool QmlSettings::speechProcessing() const
{
    return settings->GetSpeechProcessingEnabled();
}

void QmlSettings::setSpeechProcessing(bool processing)
{
    settings->SetSpeechProcessingEnabled(processing);
    emit speechProcessingChanged();
}

int QmlSettings::noiseSuppressLevel() const
{
    return settings->GetNoiseSuppressLevel();
}

void QmlSettings::setNoiseSuppressLevel(int level)
{
    settings->SetNoiseSuppressLevel(level);
    emit noiseSuppressLevelChanged();
}

int QmlSettings::echoSuppressLevel() const
{
    return settings->GetEchoSuppressLevel();
}

void QmlSettings::setEchoSuppressLevel(int level)
{
    settings->SetEchoSuppressLevel(level);
    emit echoSuppressLevelChanged();
}
#endif

bool QmlSettings::allowJoystickBackgroundEvents() const
{
    return settings->GetAllowJoystickBackgroundEvents();
}

void QmlSettings::setAllowJoystickBackgroundEvents(bool enabled)
{
    settings->SetAllowJoystickBackgroundEvents(enabled);
    emit allowJoystickBackgroundEventsChanged();
}

int QmlSettings::fpsLocalPS4() const
{
    return settings->GetFPSLocalPS4();
}

void QmlSettings::setFpsLocalPS4(int fps)
{
    settings->SetFPSLocalPS4(static_cast<ChiakiVideoFPSPreset>(fps));
    emit fpsLocalPS4Changed();
}

int QmlSettings::fpsRemotePS4() const
{
    return settings->GetFPSRemotePS4();
}

void QmlSettings::setFpsRemotePS4(int fps)
{
    settings->SetFPSRemotePS4(static_cast<ChiakiVideoFPSPreset>(fps));
    emit fpsRemotePS4Changed();
}

int QmlSettings::fpsLocalPS5() const
{
    return settings->GetFPSLocalPS5();
}

void QmlSettings::setFpsLocalPS5(int fps)
{
    settings->SetFPSLocalPS5(static_cast<ChiakiVideoFPSPreset>(fps));
    emit fpsLocalPS5Changed();
}

int QmlSettings::fpsRemotePS5() const
{
    return settings->GetFPSRemotePS5();
}

void QmlSettings::setFpsRemotePS5(int fps)
{
    settings->SetFPSRemotePS5(static_cast<ChiakiVideoFPSPreset>(fps));
    emit fpsRemotePS5Changed();
}

int QmlSettings::bitrateLocalPS4() const
{
    return settings->GetBitrateLocalPS4();
}

void QmlSettings::setBitrateLocalPS4(int bitrate)
{
    settings->SetBitrateLocalPS4(bitrate);
    emit bitrateLocalPS4Changed();
}

int QmlSettings::bitrateRemotePS4() const
{
    return settings->GetBitrateRemotePS4();
}

void QmlSettings::setBitrateRemotePS4(int bitrate)
{
    settings->SetBitrateRemotePS4(bitrate);
    emit bitrateRemotePS4Changed();
}

int QmlSettings::bitrateLocalPS5() const
{
    return settings->GetBitrateLocalPS5();
}

void QmlSettings::setBitrateLocalPS5(int bitrate)
{
    settings->SetBitrateLocalPS5(bitrate);
    emit bitrateLocalPS5Changed();
}

int QmlSettings::bitrateRemotePS5() const
{
    return settings->GetBitrateRemotePS5();
}

void QmlSettings::setBitrateRemotePS5(int bitrate)
{
    settings->SetBitrateRemotePS5(bitrate);
    emit bitrateRemotePS5Changed();
}

int QmlSettings::codecLocalPS5() const
{
    return settings->GetCodecLocalPS5();
}

void QmlSettings::setCodecLocalPS5(int codec)
{
    settings->SetCodecLocalPS5(static_cast<ChiakiCodec>(codec));
    emit codecLocalPS5Changed();
}

int QmlSettings::codecRemotePS5() const
{
    return settings->GetCodecRemotePS5();
}

void QmlSettings::setCodecRemotePS5(int codec)
{
    settings->SetCodecRemotePS5(static_cast<ChiakiCodec>(codec));
    emit codecRemotePS5Changed();
}

int QmlSettings::displayTargetContrast() const
{
    return settings->GetDisplayTargetContrast();
}
void QmlSettings::setDisplayTargetContrast(int contrast)
{
    settings->SetDisplayTargetContrast(contrast);
    emit displayTargetContrastChanged();
}

int QmlSettings::displayTargetPeak() const
{
    return settings->GetDisplayTargetPeak();
}

void QmlSettings::setDisplayTargetPeak(int peak)
{
    settings->SetDisplayTargetPeak(peak);
    emit displayTargetPeakChanged();
}

int QmlSettings::displayTargetPrim() const
{
    return settings->GetDisplayTargetPrim();
}

void QmlSettings::setDisplayTargetPrim(int prim)
{
    settings->SetDisplayTargetPrim(prim);
    emit displayTargetPrimChanged();
}

int QmlSettings::displayTargetTrc() const
{
    return settings->GetDisplayTargetTrc();
}

void QmlSettings::setDisplayTargetTrc(int trc)
{
    settings->SetDisplayTargetTrc(trc);
    emit displayTargetTrcChanged();
}

int QmlSettings::audioBufferSize() const
{
    return settings->GetAudioBufferSizeRaw();
}

void QmlSettings::setAudioBufferSize(int size)
{
    settings->SetAudioBufferSize(size);
    emit audioBufferSizeChanged();
}

int QmlSettings::audioVolume() const
{
    return settings->GetAudioVolume();
}

void QmlSettings::setAudioVolume(int volume)
{
    settings->SetAudioVolume(volume);
    emit audioVolumeChanged();
}

QString QmlSettings::audioInDevice() const
{
    return settings->GetAudioInDevice();
}

void QmlSettings::setAudioInDevice(const QString &device)
{
    settings->SetAudioInDevice(device);
    emit audioInDeviceChanged();
}

QString QmlSettings::audioOutDevice() const
{
    return settings->GetAudioOutDevice();
}

void QmlSettings::setAudioOutDevice(const QString &device)
{
    settings->SetAudioOutDevice(device);
    emit audioOutDeviceChanged();
}

QString QmlSettings::decoder() const
{
    return settings->GetHardwareDecoder();
}

void QmlSettings::setDecoder(const QString &decoder)
{
    settings->SetHardwareDecoder(decoder);
    emit decoderChanged();
}

bool QmlSettings::useZeroCopy() const
{
    return settings->GetUseZeroCopy();
}

void QmlSettings::setUseZeroCopy(bool enabled)
{
    settings->SetUseZeroCopy(enabled);
    emit useZeroCopyChanged();
}

bool QmlSettings::vulkanDeferredSwap() const
{
    return settings->GetVulkanDeferredSwap();
}

void QmlSettings::setVulkanDeferredSwap(bool enabled)
{
    settings->SetVulkanDeferredSwap(enabled);
    emit vulkanDeferredSwapChanged();
}

int QmlSettings::windowType() const
{
    return static_cast<int>(settings->GetWindowType());
}

void QmlSettings::setWindowType(int type)
{
    settings->SetWindowType(static_cast<WindowType>(type));
    emit windowTypeChanged();
}

uint QmlSettings::customResolutionWidth() const
{
    return settings->GetCustomResolutionWidth();
}

void QmlSettings::setCustomResolutionWidth(uint width)
{
    settings->SetCustomResolutionWidth(width);
    emit customResolutionWidthChanged();
}

uint QmlSettings::customResolutionHeight() const
{
    return settings->GetCustomResolutionHeight();
}

void QmlSettings::setCustomResolutionHeight(uint length)
{
    settings->SetCustomResolutionHeight(length);
    emit customResolutionHeightChanged();
}

float QmlSettings::sZoomFactor() const
{
    return settings->GetZoomFactor();
}

void QmlSettings::setSZoomFactor(float factor)
{
    settings->SetZoomFactor(factor);
    emit sZoomFactorChanged();
}

int QmlSettings::packetLossReportedMax() const
{
    return static_cast<int>(settings->GetPacketLossReportedMax() * 100);
}

void QmlSettings::setPacketLossReportedMax(int packet_loss_reported_max)
{
    float packet_loss = static_cast<float>(packet_loss_reported_max) / 100.0f;
    settings->SetPacketLossReportedMax(packet_loss);
    emit packetLossReportedMaxChanged();
}

int QmlSettings::videoPreset() const
{
    return static_cast<int>(settings->GetPlaceboPreset());
}

void QmlSettings::setVideoPreset(int preset)
{
    settings->SetPlaceboPreset(static_cast<PlaceboPreset>(preset));
    emit videoPresetChanged();
}

int QmlSettings::rendererBackend() const
{
    return static_cast<int>(settings->GetRenderBackend());
}

void QmlSettings::setRendererBackend(int backend)
{
    if (backend < static_cast<int>(RenderBackend::Vulkan) ||
        backend > static_cast<int>(RenderBackend::OpenGL)) {
        qWarning() << "Ignoring invalid renderer backend value:" << backend;
        return;
    }

    auto next_backend = static_cast<RenderBackend>(backend);
    if (settings->GetRenderBackend() == next_backend)
        return;

    settings->SetRenderBackend(next_backend);
    emit rendererBackendChanged();
}

bool QmlSettings::restartApplication()
{
    settings->Sync();
    const QString application = QCoreApplication::applicationFilePath();
    const QStringList arguments = QCoreApplication::arguments().mid(1);
    if (!QProcess::startDetached(application, arguments)) {
        qWarning() << "Failed to relaunch application";
        return false;
    }
    return true;
}

QString QmlSettings::autoConnectMac() const
{
    return settings->GetAutoConnectHost().GetServerMAC().ToString();
}

void QmlSettings::setAutoConnectMac(const QString &mac)
{
    settings->SetAutoConnectHost(QByteArray::fromHex(mac.toUtf8()));
    emit autoConnectMacChanged();
}

uint QmlSettings::wifiDroppedNotif() const
{
    return settings->GetWifiDroppedNotif();
}

void QmlSettings::setWifiDroppedNotif(const uint percent)
{
    settings->SetWifiDroppedNotif(percent);
    emit wifiDroppedNotifChanged();
}

bool QmlSettings::portGuessingEnabled() const
{
    return settings->GetPortGuessingEnabled();
}

void QmlSettings::setPortGuessingEnabled(bool enabled)
{
    settings->SetPortGuessingEnabled(enabled);
    emit portGuessingEnabledChanged();
}

int QmlSettings::portGuessCount() const
{
    return settings->GetPortGuessCount();
}

void QmlSettings::setPortGuessCount(int count)
{
    settings->SetPortGuessCount(count);
    emit portGuessCountChanged();
}

int QmlSettings::portGuessSocketCount() const
{
    return settings->GetPortGuessSocketCount();
}

void QmlSettings::setPortGuessSocketCount(int count)
{
    settings->SetPortGuessSocketCount(count);
    emit portGuessSocketCountChanged();
}

QString QmlSettings::psnAuthToken() const
{
    return settings->GetPsnAuthToken();
}

void QmlSettings::setPsnAuthToken(const QString &auth_token)
{
    settings->SetPsnAuthToken(auth_token);
    emit psnAuthTokenChanged();
}

QString QmlSettings::psnRefreshToken() const
{
    return settings->GetPsnRefreshToken();
}

void QmlSettings::setPsnRefreshToken(const QString &refresh_token)
{
    settings->SetPsnRefreshToken(refresh_token);
    emit psnRefreshTokenChanged();
}

QString QmlSettings::psnAuthTokenExpiry() const
{
    return settings->GetPsnAuthTokenExpiry();
}

void QmlSettings::setPsnAuthTokenExpiry(const QString &expiry)
{
    settings->SetPsnAuthTokenExpiry(expiry);
    emit psnAuthTokenExpiryChanged();
}

QString QmlSettings::psnAccountId() const
{
    return settings->GetPsnAccountId();
}

void QmlSettings::setPsnAccountId(const QString &account_id)
{
    settings->SetPsnAccountId(account_id);
    emit psnAccountIdChanged();
}

bool QmlSettings::mouseTouchEnabled() const
{
    return settings->GetMouseTouchEnabled();
}

void QmlSettings::setMouseTouchEnabled(bool enabled)
{
    settings->SetMouseTouchEnabled(enabled);
    emit mouseTouchEnabledChanged();
}

bool QmlSettings::keyboardEnabled() const
{
    return settings->GetKeyboardEnabled();
}

void QmlSettings::setKeyboardEnabled(bool enabled)
{
    settings->SetKeyboardEnabled(enabled);
    emit keyboardEnabledChanged();
}

bool QmlSettings::dpadTouchEnabled() const
{
    return settings->GetDpadTouchEnabled();
}

void QmlSettings::setDpadTouchEnabled(bool enabled)
{
    settings->SetDpadTouchEnabled(enabled);
    emit dpadTouchEnabledChanged();
}

uint16_t QmlSettings::dpadTouchIncrement() const
{
    return settings->GetDpadTouchIncrement();
}

void QmlSettings::setDpadTouchIncrement(uint16_t increment)
{
    settings->SetDpadTouchIncrement(increment);
    emit dpadTouchIncrementChanged();
}

uint QmlSettings::dpadTouchShortcut1() const {
	return settings->GetDpadTouchShortcut1();
}
void QmlSettings::setDpadTouchShortcut1(uint button) {
	settings->SetDpadTouchShortcut1(button);
    emit dpadTouchShortcut1Changed();
}

uint QmlSettings::dpadTouchShortcut2() const {
	return settings->GetDpadTouchShortcut2();
}
void QmlSettings::setDpadTouchShortcut2(uint button) {
	settings->SetDpadTouchShortcut2(button);
    emit dpadTouchShortcut2Changed();
}

uint QmlSettings::dpadTouchShortcut3() const {
	return settings->GetDpadTouchShortcut3();
}
void QmlSettings::setDpadTouchShortcut3(uint button) {
	settings->SetDpadTouchShortcut3(button);
    emit dpadTouchShortcut3Changed();
}

uint QmlSettings::dpadTouchShortcut4() const {
	return settings->GetDpadTouchShortcut4();
}
void QmlSettings::setDpadTouchShortcut4(uint button) {
	settings->SetDpadTouchShortcut4(button);
    emit dpadTouchShortcut4Changed();
}

bool QmlSettings::streamMenuEnabled() const
{
    return settings->GetStreamMenuEnabled();
}

void QmlSettings::setStreamMenuEnabled(bool enabled)
{
    settings->SetStreamMenuEnabled(enabled);
    emit streamMenuEnabledChanged();
}

uint QmlSettings::streamMenuShortcut1() const {
	return settings->GetStreamMenuShortcut1();
}
void QmlSettings::setStreamMenuShortcut1(uint button) {
	settings->SetStreamMenuShortcut1(button);
    emit streamMenuShortcut1Changed();
}

uint QmlSettings::streamMenuShortcut2() const {
	return settings->GetStreamMenuShortcut2();
}
void QmlSettings::setStreamMenuShortcut2(uint button) {
	settings->SetStreamMenuShortcut2(button);
    emit streamMenuShortcut2Changed();
}

uint QmlSettings::streamMenuShortcut3() const {
	return settings->GetStreamMenuShortcut3();
}
void QmlSettings::setStreamMenuShortcut3(uint button) {
	settings->SetStreamMenuShortcut3(button);
    emit streamMenuShortcut3Changed();
}

uint QmlSettings::streamMenuShortcut4() const {
	return settings->GetStreamMenuShortcut4();
}
void QmlSettings::setStreamMenuShortcut4(uint button) {
	settings->SetStreamMenuShortcut4(button);
    emit streamMenuShortcut4Changed();
}

QString QmlSettings::currentProfile() const
{
    return settings->GetCurrentProfile();
}

void QmlSettings::setCurrentProfile(const QString &profile)
{
    settings->SetCurrentProfile(profile);
    emit currentProfileChanged();
}

int QmlSettings::placeboUpscaler() const
{
    return static_cast<int>(settings->GetPlaceboUpscaler());
}
void QmlSettings::setPlaceboUpscaler(int upscaler)
{
    settings->SetPlaceboUpscaler(static_cast<PlaceboUpscaler>(upscaler));
    emit placeboUpscalerChanged();
    emit placeboChanged();
}

int QmlSettings::placeboPlaneUpscaler() const
{
    return static_cast<int>(settings->GetPlaceboPlaneUpscaler());
}
void QmlSettings::setPlaceboPlaneUpscaler(int upscaler)
{
    settings->SetPlaceboPlaneUpscaler(static_cast<PlaceboUpscaler>(upscaler));
    emit placeboPlaneUpscalerChanged();
    emit placeboChanged();
}

int QmlSettings::placeboDownscaler() const
{
    return static_cast<int>(settings->GetPlaceboDownscaler());
}
void QmlSettings::setPlaceboDownscaler(int downscaler)
{
    settings->SetPlaceboDownscaler(static_cast<PlaceboDownscaler>(downscaler));
    emit placeboDownscalerChanged();
    emit placeboChanged();
}

int QmlSettings::placeboPlaneDownscaler() const
{
    return static_cast<int>(settings->GetPlaceboPlaneDownscaler());
}
void QmlSettings::setPlaceboPlaneDownscaler(int downscaler)
{
    settings->SetPlaceboPlaneDownscaler(static_cast<PlaceboDownscaler>(downscaler));
    emit placeboPlaneDownscalerChanged();
    emit placeboChanged();
}

int QmlSettings::placeboFrameMixer() const
{
    return static_cast<int>(settings->GetPlaceboFrameMixer());
}
bool QmlSettings::directFrameMapping() const
{
    return settings->GetDirectFrameMapping();
}

void QmlSettings::setDirectFrameMapping(bool enabled)
{
    if (enabled == settings->GetDirectFrameMapping())
        return;
    settings->SetDirectFrameMapping(enabled);
    emit directFrameMappingChanged();
}

void QmlSettings::setPlaceboFrameMixer(int mixer)
{
    settings->SetPlaceboFrameMixer(static_cast<PlaceboFrameMixer>(mixer));
    emit placeboFrameMixerChanged();
    emit placeboChanged();
}

bool QmlSettings::placeboDeinterlaceEnabled() const
{
    return settings->GetPlaceboDeinterlaceEnabled();
}

void QmlSettings::setPlaceboDeinterlaceEnabled(bool enabled)
{
    settings->SetPlaceboDeinterlaceEnabled(enabled);
    emit placeboDeinterlaceEnabledChanged();
    emit placeboChanged();
}

int QmlSettings::placeboDeinterlacePreset() const
{
    return static_cast<int>(settings->GetPlaceboDeinterlacePreset());
}

void QmlSettings::setPlaceboDeinterlacePreset(int preset)
{
    settings->SetPlaceboDeinterlacePreset(static_cast<PlaceboDeinterlacePreset>(preset));
    emit placeboDeinterlacePresetChanged();
    emit placeboChanged();
}

int QmlSettings::placeboDeinterlaceAlgorithm() const
{
    return static_cast<int>(settings->GetPlaceboDeinterlaceAlgorithm());
}

void QmlSettings::setPlaceboDeinterlaceAlgorithm(int algorithm)
{
    settings->SetPlaceboDeinterlaceAlgorithm(static_cast<PlaceboDeinterlaceAlgorithm>(algorithm));
    emit placeboDeinterlaceAlgorithmChanged();
    emit placeboChanged();
}

bool QmlSettings::placeboDeinterlaceSkipSpatial() const
{
    return settings->GetPlaceboDeinterlaceSkipSpatial();
}

void QmlSettings::setPlaceboDeinterlaceSkipSpatial(bool skip)
{
    settings->SetPlaceboDeinterlaceSkipSpatial(skip);
    emit placeboDeinterlaceSkipSpatialChanged();
    emit placeboChanged();
}

float QmlSettings::placeboAntiringingStrength() const
{
    return settings->GetPlaceboAntiringingStrength();
}
void QmlSettings::setPlaceboAntiringingStrength(float strength)
{
    settings->SetPlaceboAntiringingStrength(strength);
    emit placeboAntiringingStrengthChanged();
    emit placeboChanged();
}

bool QmlSettings::placeboDebandEnabled() const
{
    return settings->GetPlaceboDebandEnabled();
}
void QmlSettings::setPlaceboDebandEnabled(bool enabled)
{
    settings->SetPlaceboDebandEnabled(enabled);
    emit placeboDebandEnabledChanged();
    emit placeboChanged();
}

int QmlSettings::placeboDebandPreset() const
{
    return static_cast<int>(settings->GetPlaceboDebandPreset());
}
void QmlSettings::setPlaceboDebandPreset(int preset)
{
    settings->SetPlaceboDebandPreset(static_cast<PlaceboDebandPreset>(preset));
    emit placeboDebandPresetChanged();
    emit placeboChanged();
}

int QmlSettings::placeboDebandIterations() const
{
    return settings->GetPlaceboDebandIterations();
}
void QmlSettings::setPlaceboDebandIterations(int iterations)
{
    settings->SetPlaceboDebandIterations(iterations);
    emit placeboDebandIterationsChanged();
    emit placeboChanged();
}

float QmlSettings::placeboDebandThreshold() const
{
    return settings->GetPlaceboDebandThreshold();
}
void QmlSettings::setPlaceboDebandThreshold(float threshold)
{
    settings->SetPlaceboDebandThreshold(threshold);
    emit placeboDebandThresholdChanged();
    emit placeboChanged();
}

float QmlSettings::placeboDebandRadius() const
{
    return settings->GetPlaceboDebandRadius();
}
void QmlSettings::setPlaceboDebandRadius(float radius)
{
    settings->SetPlaceboDebandRadius(radius);
    emit placeboDebandRadiusChanged();
    emit placeboChanged();
}

float QmlSettings::placeboDebandGrain() const
{
    return settings->GetPlaceboDebandGrain();
}
void QmlSettings::setPlaceboDebandGrain(float grain)
{
    settings->SetPlaceboDebandGrain(grain);
    emit placeboDebandGrainChanged();
    emit placeboChanged();
}

bool QmlSettings::placeboSigmoidEnabled() const
{
    return settings->GetPlaceboSigmoidEnabled();
}
void QmlSettings::setPlaceboSigmoidEnabled(bool enabled)
{
    settings->SetPlaceboSigmoidEnabled(enabled);
    emit placeboSigmoidEnabledChanged();
    emit placeboChanged();
}

int QmlSettings::placeboSigmoidPreset() const
{
    return static_cast<int>(settings->GetPlaceboSigmoidPreset());
}
void QmlSettings::setPlaceboSigmoidPreset(int preset)
{
    settings->SetPlaceboSigmoidPreset(static_cast<PlaceboSigmoidPreset>(preset));
    emit placeboSigmoidPresetChanged();
    emit placeboChanged();
}

float QmlSettings::placeboSigmoidCenter() const
{
    return settings->GetPlaceboSigmoidCenter();
}
void QmlSettings::setPlaceboSigmoidCenter(float center)
{
    settings->SetPlaceboSigmoidCenter(center);
    emit placeboSigmoidCenterChanged();
    emit placeboChanged();
}

float QmlSettings::placeboSigmoidSlope() const
{
    return settings->GetPlaceboSigmoidSlope();
}
void QmlSettings::setPlaceboSigmoidSlope(float slope)
{
    settings->SetPlaceboSigmoidSlope(slope);
    emit placeboSigmoidSlopeChanged();
    emit placeboChanged();
}

bool QmlSettings::placeboColorAdjustmentEnabled() const
{
    return settings->GetPlaceboColorAdjustmentEnabled();
}
void QmlSettings::setPlaceboColorAdjustmentEnabled(bool enabled)
{
    settings->SetPlaceboColorAdjustmentEnabled(enabled);
    emit placeboColorAdjustmentEnabledChanged();
    emit placeboChanged();
}

int QmlSettings::placeboColorAdjustmentPreset() const
{
    return static_cast<int>(settings->GetPlaceboColorAdjustmentPreset());
}
void QmlSettings::setPlaceboColorAdjustmentPreset(int preset)
{
    settings->SetPlaceboColorAdjustmentPreset(static_cast<PlaceboColorAdjustmentPreset>(preset));
    emit placeboColorAdjustmentPresetChanged();
    emit placeboChanged();
}

float QmlSettings::placeboColorAdjustmentBrightness() const
{
    return settings->GetPlaceboColorAdjustmentBrightness();
}
void QmlSettings::setPlaceboColorAdjustmentBrightness(float brightness)
{
    settings->SetPlaceboColorAdjustmentBrightness(brightness);
    emit placeboColorAdjustmentBrightnessChanged();
    emit placeboChanged();
}

float QmlSettings::placeboColorAdjustmentContrast() const
{
    return settings->GetPlaceboColorAdjustmentContrast();
}
void QmlSettings::setPlaceboColorAdjustmentContrast(float contrast)
{
    settings->SetPlaceboColorAdjustmentContrast(contrast);
    emit placeboColorAdjustmentContrastChanged();
    emit placeboChanged();
}

float QmlSettings::placeboColorAdjustmentSaturation() const
{
    return settings->GetPlaceboColorAdjustmentSaturation();
}
void QmlSettings::setPlaceboColorAdjustmentSaturation(float saturation)
{
    settings->SetPlaceboColorAdjustmentSaturation(saturation);
    emit placeboColorAdjustmentSaturationChanged();
    emit placeboChanged();
}

float QmlSettings::placeboColorAdjustmentHue() const
{
    return settings->GetPlaceboColorAdjustmentHue();
}
void QmlSettings::setPlaceboColorAdjustmentHue(float hue)
{
    settings->SetPlaceboColorAdjustmentHue(hue);
    emit placeboColorAdjustmentHueChanged();
    emit placeboChanged();
}

float QmlSettings::placeboColorAdjustmentGamma() const
{
    return settings->GetPlaceboColorAdjustmentGamma();
}
void QmlSettings::setPlaceboColorAdjustmentGamma(float gamma)
{
    settings->SetPlaceboColorAdjustmentGamma(gamma);
    emit placeboColorAdjustmentGammaChanged();
    emit placeboChanged();
}

float QmlSettings::placeboColorAdjustmentTemperature() const
{
    return settings->GetPlaceboColorAdjustmentTemperature();
}
void QmlSettings::setPlaceboColorAdjustmentTemperature(float temperature)
{
    settings->SetPlaceboColorAdjustmentTemperature(temperature);
    emit placeboColorAdjustmentTemperatureChanged();
    emit placeboChanged();
}

bool QmlSettings::placeboPeakDetectionEnabled() const
{
    return settings->GetPlaceboPeakDetectionEnabled();
}
void QmlSettings::setPlaceboPeakDetectionEnabled(bool enabled)
{
    settings->SetPlaceboPeakDetectionEnabled(enabled);
    emit placeboPeakDetectionEnabledChanged();
    emit placeboChanged();
}

int QmlSettings::placeboPeakDetectionPreset() const
{
    return static_cast<int>(settings->GetPlaceboPeakDetectionPreset());
}
void QmlSettings::setPlaceboPeakDetectionPreset(int preset)
{
    settings->SetPlaceboPeakDetectionPreset(static_cast<PlaceboPeakDetectionPreset>(preset));
    emit placeboPeakDetectionPresetChanged();
    emit placeboChanged();
}

float QmlSettings::placeboPeakDetectionPeakSmoothingPeriod() const
{
    return settings->GetPlaceboPeakDetectionPeakSmoothingPeriod();
}
void QmlSettings::setPlaceboPeakDetectionPeakSmoothingPeriod(float period)
{
    settings->SetPlaceboPeakDetectionPeakSmoothingPeriod(period);
    emit placeboPeakDetectionPeakSmoothingPeriodChanged();
    emit placeboChanged();
}

float QmlSettings::placeboPeakDetectionSceneThresholdLow() const
{
    return settings->GetPlaceboPeakDetectionSceneThresholdLow();
}
void QmlSettings::setPlaceboPeakDetectionSceneThresholdLow(float threshold_low)
{
    settings->SetPlaceboPeakDetectionSceneThresholdLow(threshold_low);
    emit placeboPeakDetectionSceneThresholdLowChanged();
    emit placeboChanged();
}

float QmlSettings::placeboPeakDetectionSceneThresholdHigh() const
{
    return settings->GetPlaceboPeakDetectionSceneThresholdHigh();
}
void QmlSettings::setPlaceboPeakDetectionSceneThresholdHigh(float threshold_high)
{
    settings->SetPlaceboPeakDetectionSceneThresholdHigh(threshold_high);
    emit placeboPeakDetectionSceneThresholdHighChanged();
    emit placeboChanged();
}

float QmlSettings::placeboPeakDetectionPeakPercentile() const
{
    return settings->GetPlaceboPeakDetectionPeakPercentile();
}
void QmlSettings::setPlaceboPeakDetectionPeakPercentile(float percentile)
{
    settings->SetPlaceboPeakDetectionPeakPercentile(percentile);
    emit placeboPeakDetectionPeakPercentileChanged();
    emit placeboChanged();
}

float QmlSettings::placeboPeakDetectionBlackCutoff() const
{
    return settings->GetPlaceboPeakDetectionBlackCutoff();
}
void QmlSettings::setPlaceboPeakDetectionBlackCutoff(float cutoff)
{
    settings->SetPlaceboPeakDetectionBlackCutoff(cutoff);
    emit placeboPeakDetectionBlackCutoffChanged();
    emit placeboChanged();
}

bool QmlSettings::placeboPeakDetectionAllowDelayedPeak() const
{
    return settings->GetPlaceboPeakDetectionAllowDelayedPeak();
}
void QmlSettings::setPlaceboPeakDetectionAllowDelayedPeak(bool allowed)
{
    settings->SetPlaceboPeakDetectionAllowDelayedPeak(allowed);
    emit placeboPeakDetectionAllowDelayedPeakChanged();
    emit placeboChanged();
}

bool QmlSettings::placeboColorMappingEnabled() const
{
    return settings->GetPlaceboColorMappingEnabled();
}
void QmlSettings::setPlaceboColorMappingEnabled(bool enabled)
{
    settings->SetPlaceboColorMappingEnabled(enabled);
    emit placeboColorMappingEnabledChanged();
    emit placeboChanged();
}

int QmlSettings::placeboColorMappingPreset() const
{
    return static_cast<int>(settings->GetPlaceboColorMappingPreset());
}
void QmlSettings::setPlaceboColorMappingPreset(int preset)
{
    settings->SetPlaceboColorMappingPreset(static_cast<PlaceboColorMappingPreset>(preset));
    emit placeboColorMappingPresetChanged();
    emit placeboChanged();
}

int QmlSettings::placeboGamutMappingFunction() const
{
    return static_cast<int>(settings->GetPlaceboGamutMappingFunction());
}
void QmlSettings::setPlaceboGamutMappingFunction(int function)
{
    settings->SetPlaceboGamutMappingFunction(static_cast<PlaceboGamutMappingFunction>(function));
    emit placeboGamutMappingFunctionChanged();
    emit placeboChanged();
}

float QmlSettings::placeboGamutMappingPerceptualDeadzone() const
{
    return settings->GetPlaceboGamutMappingPerceptualDeadzone();
}
void QmlSettings::setPlaceboGamutMappingPerceptualDeadzone(float deadzone)
{
    settings->SetPlaceboGamutMappingPerceptualDeadzone(deadzone);
    emit placeboGamutMappingPerceptualDeadzoneChanged();
    emit placeboChanged();
}

float QmlSettings::placeboGamutMappingPerceptualStrength() const
{
    return settings->GetPlaceboGamutMappingPerceptualStrength();
}
void QmlSettings::setPlaceboGamutMappingPerceptualStrength(float strength)
{
    settings->SetPlaceboGamutMappingPerceptualStrength(strength);
    emit placeboGamutMappingPerceptualStrengthChanged();
    emit placeboChanged();
}

float QmlSettings::placeboGamutMappingColorimetricGamma() const
{
    return settings->GetPlaceboGamutMappingColorimetricGamma();
}
void QmlSettings::setPlaceboGamutMappingColorimetricGamma(float gamma)
{
    settings->SetPlaceboGamutMappingColorimetricGamma(gamma);
    emit placeboGamutMappingColorimetricGammaChanged();
    emit placeboChanged();
}

float QmlSettings::placeboGamutMappingSoftClipKnee() const
{
    return settings->GetPlaceboGamutMappingSoftClipKnee();
}
void QmlSettings::setPlaceboGamutMappingSoftClipKnee(float knee)
{
    settings->SetPlaceboGamutMappingSoftClipKnee(knee);
    emit placeboGamutMappingSoftClipKneeChanged();
    emit placeboChanged();
}

float QmlSettings::placeboGamutMappingSoftClipDesat() const
{
    return settings->GetPlaceboGamutMappingSoftClipDesat();
}
void QmlSettings::setPlaceboGamutMappingSoftClipDesat(float desat)
{
    settings->SetPlaceboGamutMappingSoftClipDesat(desat);
    emit placeboGamutMappingSoftClipDesatChanged();
    emit placeboChanged();
}

int QmlSettings::placeboGamutMappingLut3dSizeI() const
{
    return settings->GetPlaceboGamutMappingLut3dSizeI();
}
void QmlSettings::setPlaceboGamutMappingLut3dSizeI(int size)
{
    settings->SetPlaceboGamutMappingLut3dSizeI(size);
    emit placeboGamutMappingLut3dSizeIChanged();
    emit placeboChanged();
}

int QmlSettings::placeboGamutMappingLut3dSizeC() const
{
    return settings->GetPlaceboGamutMappingLut3dSizeC();
}
void QmlSettings::setPlaceboGamutMappingLut3dSizeC(int size)
{
    settings->SetPlaceboGamutMappingLut3dSizeC(size);
    emit placeboGamutMappingLut3dSizeCChanged();
    emit placeboChanged();
}

int QmlSettings::placeboGamutMappingLut3dSizeH() const
{
    return settings->GetPlaceboGamutMappingLut3dSizeH();
}
void QmlSettings::setPlaceboGamutMappingLut3dSizeH(int size)
{
    settings->SetPlaceboGamutMappingLut3dSizeH(size);
    emit placeboGamutMappingLut3dSizeHChanged();
    emit placeboChanged();
}

bool QmlSettings::placeboGamutMappingLut3dTricubicEnabled() const
{
    return settings->GetPlaceboGamutMappingLut3dTricubicEnabled();
}
void QmlSettings::setPlaceboGamutMappingLut3dTricubicEnabled(bool enabled)
{
    settings->SetPlaceboGamutMappingLut3dTricubicEnabled(enabled);
    emit placeboGamutMappingLut3dTricubicEnabledChanged();
    emit placeboChanged();
}

bool QmlSettings::placeboGamutMappingGamutExpansionEnabled() const
{
    return settings->GetPlaceboGamutMappingGamutExpansionEnabled();
}
void QmlSettings::setPlaceboGamutMappingGamutExpansionEnabled(bool enabled)
{
    settings->SetPlaceboGamutMappingGamutExpansionEnabled(enabled);
    emit placeboGamutMappingGamutExpansionEnabledChanged();
    emit placeboChanged();
}

int QmlSettings::placeboToneMappingFunction() const
{
    return static_cast<int>(settings->GetPlaceboToneMappingFunction());
}
void QmlSettings::setPlaceboToneMappingFunction(int function)
{
    settings->SetPlaceboToneMappingFunction(static_cast<PlaceboToneMappingFunction>(function));
    emit placeboToneMappingFunctionChanged();
    emit placeboChanged();
}

float QmlSettings::placeboToneMappingKneeAdaptation() const
{
    return settings->GetPlaceboToneMappingKneeAdaptation();
}
void QmlSettings::setPlaceboToneMappingKneeAdaptation(float adaptation)
{
    settings->SetPlaceboToneMappingKneeAdaptation(adaptation);
    emit placeboToneMappingKneeAdaptationChanged();
    emit placeboChanged();
}

float QmlSettings::placeboToneMappingKneeMinimum() const
{
    return settings->GetPlaceboToneMappingKneeMinimum();
}
void QmlSettings::setPlaceboToneMappingKneeMinimum(float min)
{
    settings->SetPlaceboToneMappingKneeMinimum(min);
    emit placeboToneMappingKneeMinimumChanged();
    emit placeboChanged();
}

float QmlSettings::placeboToneMappingKneeMaximum() const
{
    return settings->GetPlaceboToneMappingKneeMaximum();
}
void QmlSettings::setPlaceboToneMappingKneeMaximum(float max)
{
    settings->SetPlaceboToneMappingKneeMaximum(max);
    emit placeboToneMappingKneeMaximumChanged();
    emit placeboChanged();
}

float QmlSettings::placeboToneMappingKneeDefault() const
{
    return settings->GetPlaceboToneMappingKneeDefault();
}
void QmlSettings::setPlaceboToneMappingKneeDefault(float default_knee)
{
    settings->SetPlaceboToneMappingKneeDefault(default_knee);
    emit placeboToneMappingKneeDefaultChanged();
    emit placeboChanged();
}

float QmlSettings::placeboToneMappingKneeOffset() const
{
    return settings->GetPlaceboToneMappingKneeOffset();
}
void QmlSettings::setPlaceboToneMappingKneeOffset(float offset)
{
    settings->SetPlaceboToneMappingKneeOffset(offset);
    emit placeboToneMappingKneeOffsetChanged();
    emit placeboChanged();
}

float QmlSettings::placeboToneMappingSlopeTuning() const
{
    return settings->GetPlaceboToneMappingSlopeTuning();
}
void QmlSettings::setPlaceboToneMappingSlopeTuning(float tuning)
{
    settings->SetPlaceboToneMappingSlopeTuning(tuning);
    emit placeboToneMappingSlopeTuningChanged();
    emit placeboChanged();
}

float QmlSettings::placeboToneMappingSlopeOffset() const
{
    return settings->GetPlaceboToneMappingSlopeOffset();
}
void QmlSettings::setPlaceboToneMappingSlopeOffset(float offset)
{
    settings->SetPlaceboToneMappingSlopeOffset(offset);
    emit placeboToneMappingSlopeOffsetChanged();
    emit placeboChanged();
}

float QmlSettings::placeboToneMappingSplineContrast() const
{
    return settings->GetPlaceboToneMappingSplineContrast();
}
void QmlSettings::setPlaceboToneMappingSplineContrast(float contrast)
{
    settings->SetPlaceboToneMappingSplineContrast(contrast);
    emit placeboToneMappingSplineContrastChanged();
    emit placeboChanged();
}

float QmlSettings::placeboToneMappingReinhardContrast() const
{
    return settings->GetPlaceboToneMappingReinhardContrast();
}
void QmlSettings::setPlaceboToneMappingReinhardContrast(float contrast)
{
    settings->SetPlaceboToneMappingReinhardContrast(contrast);
    emit placeboToneMappingReinhardContrastChanged();
    emit placeboChanged();
}

float QmlSettings::placeboToneMappingLinearKnee() const
{
    return settings->GetPlaceboToneMappingLinearKnee();
}
void QmlSettings::setPlaceboToneMappingLinearKnee(float knee)
{
    settings->SetPlaceboToneMappingLinearKnee(knee);
    emit placeboToneMappingLinearKneeChanged();
    emit placeboChanged();
}

float QmlSettings::placeboToneMappingExposure() const
{
    return settings->GetPlaceboToneMappingExposure();
}
void QmlSettings::setPlaceboToneMappingExposure(float exposure)
{
    settings->SetPlaceboToneMappingExposure(exposure);
    emit placeboToneMappingExposureChanged();
    emit placeboChanged();
}

bool QmlSettings::placeboToneMappingInverseToneMappingEnabled() const
{
    return settings->GetPlaceboToneMappingInverseToneMappingEnabled();
}
void QmlSettings::setPlaceboToneMappingInverseToneMappingEnabled(bool enabled)
{
    settings->SetPlaceboToneMappingInverseToneMappingEnabled(enabled);
    emit placeboToneMappingInverseToneMappingEnabledChanged();
    emit placeboChanged();
}

int QmlSettings::placeboToneMappingMetadata() const
{
    return static_cast<int>(settings->GetPlaceboToneMappingMetadata());
}
void QmlSettings::setPlaceboToneMappingMetadata(int metadata)
{
    settings->SetPlaceboToneMappingMetadata(static_cast<PlaceboToneMappingMetadata>(metadata));
    emit placeboToneMappingMetadataChanged();
    emit placeboChanged();
}

int QmlSettings::placeboToneMappingToneLutSize() const
{
    return settings->GetPlaceboToneMappingToneLutSize();
}
void QmlSettings::setPlaceboToneMappingToneLutSize(int size)
{
    settings->SetPlaceboToneMappingToneLutSize(size);
    emit placeboToneMappingToneLutSizeChanged();
    emit placeboChanged();
}

float QmlSettings::placeboToneMappingContrastRecovery() const
{
    return settings->GetPlaceboToneMappingContrastRecovery();
}
void QmlSettings::setPlaceboToneMappingContrastRecovery(float recovery)
{
    settings->SetPlaceboToneMappingContrastRecovery(recovery);
    emit placeboToneMappingContrastRecoveryChanged();
    emit placeboChanged();
}

float QmlSettings::placeboToneMappingContrastSmoothness() const
{
    return settings->GetPlaceboToneMappingContrastSmoothness();
}
void QmlSettings::setPlaceboToneMappingContrastSmoothness(float smoothness)
{
    settings->SetPlaceboToneMappingContrastSmoothness(smoothness);
    emit placeboToneMappingContrastSmoothnessChanged();
    emit placeboChanged();
}

QString QmlSettings::logDirectory() const
{
    return GetLogBaseDir();
}

void QmlSettings::clearKeyMapping()
{
    settings->ClearKeyMapping();
    emit controllerMappingChanged();
}

QStringList QmlSettings::availableDecoders() const
{
    static QSet<QString> allowed = {
        "vulkan",
#if defined(Q_OS_LINUX)
        "vaapi",
        "cuda",
#elif defined(Q_OS_MACOS)
        "videotoolbox",
#elif defined(Q_OS_WIN)
        "d3d11va",
        "cuda",
#endif
    };
    QStringList out = {"none"};
    enum AVHWDeviceType hw_dev = AV_HWDEVICE_TYPE_NONE;
    while (true) {
        hw_dev = av_hwdevice_iterate_types(hw_dev);
        if (hw_dev == AV_HWDEVICE_TYPE_NONE)
            break;
        const QString name = QString::fromUtf8(av_hwdevice_get_type_name(hw_dev));
        if (allowed.contains(name) && hwDecoderRuntimeAvailable(name))
            out.append(name);
    }
    out.append("auto");
    return out;
}

QStringList QmlSettings::availableAudioOutDevices() const
{
    return audio_out_devices;
}

QStringList QmlSettings::availableAudioInDevices() const
{
    return audio_in_devices;
}

QVariantList QmlSettings::registeredHosts() const
{
    QVariantList out;
    for (const auto &host : settings->GetRegisteredHosts()) {
        QVariantMap m;
        m["name"] = host.GetServerNickname();
        m["mac"] = host.GetServerMAC().ToString();
        m["ps5"] = chiaki_target_is_ps5(host.GetTarget());
        out.append(m);
    }
    return out;
}

QStringList QmlSettings::profiles() const
{
    QStringList out = settings->GetProfiles();
    out.prepend("default");
    out.append("create new profile");
    return out;
}

void QmlSettings::deleteProfile(QString profile)
{
    if(!profile.isEmpty())
        settings->DeleteProfile(std::move(profile));
}

QVariantList QmlSettings::controllerMapping() const
{
    QVariantList out;
    auto key_map = settings->GetControllerMapping();
    for (auto it = key_map.cbegin(); it != key_map.cend(); ++it) {
        QVariantMap m;
        m["buttonName"] = Settings::GetChiakiControllerButtonName(it.key());
        m["buttonValue"] = it.key();
        m["keyName"] = QKeySequence(it.value()).toString();
        out.append(m);
    }
    return out;
}

QString QmlSettings::stringForDpadShortcut() const
{
    QString shortcut_string = "";
    bool plus_next = false;
    uint shortcut1 = settings->GetDpadTouchShortcut1();
    uint shortcut2 = settings->GetDpadTouchShortcut2();
    uint shortcut3 = settings->GetDpadTouchShortcut3();
    uint shortcut4 = settings->GetDpadTouchShortcut4();
    if(shortcut1)
    {
        shortcut_string.append(Settings::GetChiakiControllerButtonName(1 << (shortcut1 - 1)));
        plus_next = true;
    }
    if(shortcut2)
    {
        if(plus_next)
            shortcut_string.append("+");
        plus_next = true;
        shortcut_string.append(Settings::GetChiakiControllerButtonName(1 << (shortcut2 - 1)));
    }
    if(shortcut3)
    {
        if(plus_next)
            shortcut_string.append("+");
        plus_next = true;
        shortcut_string.append(Settings::GetChiakiControllerButtonName(1 << (shortcut3 - 1)));
    }
    if(shortcut4)
    {
        if(plus_next)
            shortcut_string.append("+");
        shortcut_string.append(Settings::GetChiakiControllerButtonName(1 << (shortcut4 - 1)));
    }
    return shortcut_string;
}

QString QmlSettings::stringForStreamMenuShortcut() const
{
    QString shortcut_string = "";
    bool plus_next = false;
    uint shortcut1 = settings->GetStreamMenuShortcut1();
    uint shortcut2 = settings->GetStreamMenuShortcut2();
    uint shortcut3 = settings->GetStreamMenuShortcut3();
    uint shortcut4 = settings->GetStreamMenuShortcut4();
    if(shortcut1)
    {
        shortcut_string.append(Settings::GetChiakiControllerButtonName(1 << (shortcut1 - 1)));
        plus_next = true;
    }
    if(shortcut2)
    {
        if(plus_next)
            shortcut_string.append("+");
        plus_next = true;
        shortcut_string.append(Settings::GetChiakiControllerButtonName(1 << (shortcut2 - 1)));
    }
    if(shortcut3)
    {
        if(plus_next)
            shortcut_string.append("+");
        plus_next = true;
        shortcut_string.append(Settings::GetChiakiControllerButtonName(1 << (shortcut3 - 1)));
    }
    if(shortcut4)
    {
        if(plus_next)
            shortcut_string.append("+");
        shortcut_string.append(Settings::GetChiakiControllerButtonName(1 << (shortcut4 - 1)));
    }
    return shortcut_string;
}

void QmlSettings::deleteRegisteredHost(int index)
{
    settings->RemoveRegisteredHost(settings->GetRegisteredHosts().value(index).GetServerMAC());
}

void QmlSettings::refreshAudioDevices()
{
    using Result = QPair<QStringList,QStringList>;
    auto watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher]() {
        watcher->deleteLater();
        const Result result = watcher->result();
        audio_in_devices = result.first;
        audio_out_devices = result.second;
        emit audioDevicesChanged();
    });
    watcher->setFuture(QtConcurrent::run([]() {
        Result res;
        int count = SDL_GetNumAudioDevices(true);
        for (int i = 0; i < count; i++)
            res.first.append(SDL_GetAudioDeviceName(i, true));
        count = SDL_GetNumAudioDevices(false);
        for (int i = 0; i < count; i++)
            res.second.append(SDL_GetAudioDeviceName(i, false));
        return res;
    }));
}

QString QmlSettings::changeControllerKey(int button, int key)
{
    Qt::Key qt_key = Qt::Key(key);
    settings->SetControllerButtonMapping(button, qt_key);
    return QKeySequence(qt_key).toString();
}

void QmlSettings::setSettings(Settings *new_settings)
{
    settings = new_settings;
    connect(settings, &Settings::RegisteredHostsUpdated, this, &QmlSettings::registeredHostsChanged);
    connect(settings, &Settings::ProfilesUpdated, this, &QmlSettings::profilesChanged);
    refreshAllKeys();
    applyGyroSteerConfig();
}

bool QmlSettings::gyroSteering() const
{
	return settings->GetGyroSteeringEnabled();
}

void QmlSettings::setGyroSteering(bool enabled)
{
	settings->SetGyroSteeringEnabled(enabled);
	emit gyroSteeringChanged();
	applyGyroSteerConfig();
}

qreal QmlSettings::gyroSteeringSensitivity() const
{
	return settings->GetGyroSteeringSensitivity();
}

void QmlSettings::setGyroSteeringSensitivity(qreal value)
{
	settings->SetGyroSteeringSensitivity((float)value);
	emit gyroSteeringChanged();
	applyGyroSteerConfig();
}

qreal QmlSettings::gyroSteeringDeadzone() const
{
	return settings->GetGyroSteeringDeadzone();
}

void QmlSettings::setGyroSteeringDeadzone(qreal value)
{
	settings->SetGyroSteeringDeadzone((float)value);
	emit gyroSteeringChanged();
	applyGyroSteerConfig();
}

bool QmlSettings::gyroSteeringInvert() const
{
	return settings->GetGyroSteeringInvert();
}

void QmlSettings::setGyroSteeringInvert(bool invert)
{
	settings->SetGyroSteeringInvert(invert);
	emit gyroSteeringChanged();
	applyGyroSteerConfig();
}

void QmlSettings::applyGyroSteerConfig()
{
	ControllerManager::GetInstance()->ApplyGyroSteerSettings(settings);
}

float QmlSettings::gyroSteerAngle()
{
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	auto bridge = ControllerManager::GetInstance()->GetGyroSteerBridge();
	if(!bridge)
		return 0.0f;
	return bridge->GetAngleDeg();
#else
	return 0.0f;
#endif
}

float QmlSettings::gyroSteerLeftX()
{
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	auto bridge = ControllerManager::GetInstance()->GetGyroSteerBridge();
	if(!bridge)
		return 0.0f;
	return bridge->GetLeftX();
#else
	return 0.0f;
#endif
}

void QmlSettings::setGyroSteerRestPoint()
{
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	auto bridge = ControllerManager::GetInstance()->GetGyroSteerBridge();
	if(bridge)
		bridge->SetRestPoint();
#endif
}

void QmlSettings::refreshAllKeys()
{
    emit remotePlayAskChanged();
    emit addSteamShortcutAskChanged();
    emit hideCursorChanged();
    emit audioVideoDisabledChanged();
    emit resolutionLocalPS4Changed();
    emit resolutionRemotePS4Changed();
    emit resolutionLocalPS5Changed();
    emit resolutionRemotePS5Changed();
    emit disconnectActionChanged();
    emit suspendActionChanged();
    emit logVerboseChanged();
    emit logSanitizeChanged();
    emit vSyncEnabledChanged();
    emit hapticOverrideChanged();
    emit ds5GyroFixChanged();
    emit hapticsAntiLatencyChanged();
    emit hapticsAntiLatencyMsChanged();
    emit rumbleHapticsIntensityChanged();
    emit buttonsByPositionChanged();
    emit allowJoystickBackgroundEventsChanged();
    emit startMicUnmutedChanged();
    emit showStreamStatsChanged();
    emit streamerModeChanged();
#ifdef CHIAKI_GUI_ENABLE_STEAMDECK_NATIVE
    emit verticalDeckChanged();
    emit steamDeckHapticsChanged();
#endif
#ifdef CHIAKI_GUI_ENABLE_SPEEX
    emit speechProcessingChanged();
    emit noiseSuppressLevelChanged();
    emit echoSuppressLevelChanged();
#endif
    emit displayTargetContrastChanged();
    emit displayTargetPeakChanged();
    emit displayTargetPrimChanged();
    emit displayTargetTrcChanged();
    emit fullscreenDoubleClickChanged();
    emit fpsLocalPS4Changed();
    emit fpsRemotePS4Changed();
    emit fpsLocalPS5Changed();
    emit fpsRemotePS5Changed();
    emit bitrateLocalPS4Changed();
    emit bitrateRemotePS4Changed();
    emit bitrateLocalPS5Changed();
    emit bitrateRemotePS5Changed();
    emit codecLocalPS5Changed();
    emit codecRemotePS5Changed();
    emit audioBufferSizeChanged();
    emit audioVolumeChanged();
    emit audioOutDeviceChanged();
    emit audioInDeviceChanged();
    emit wifiDroppedNotifChanged();
    emit portGuessingEnabledChanged();
    emit portGuessCountChanged();
    emit portGuessSocketCountChanged();
    emit decoderChanged();
    emit windowTypeChanged();
    emit customResolutionWidthChanged();
    emit customResolutionHeightChanged();
    emit sZoomFactorChanged();
    emit videoPresetChanged();
    emit rendererBackendChanged();
    emit vulkanDeferredSwapChanged();
    emit autoConnectMacChanged();
    emit audioDevicesChanged();
    emit registeredHostsChanged();
    emit psnAuthTokenChanged();
    emit psnRefreshTokenChanged();
    emit psnAuthTokenExpiryChanged();
    emit psnAccountIdChanged();
    emit mouseTouchEnabledChanged();
    emit keyboardEnabledChanged();
    emit dpadTouchEnabledChanged();
    emit iDROnFECFailureEnabledChanged();
    emit dpadTouchIncrementChanged();
    emit dpadTouchShortcut1Changed();
    emit dpadTouchShortcut2Changed();
    emit dpadTouchShortcut3Changed();
    emit dpadTouchShortcut4Changed();
    emit streamMenuEnabledChanged();
    emit streamMenuShortcut1Changed();
    emit streamMenuShortcut2Changed();
    emit streamMenuShortcut3Changed();
    emit streamMenuShortcut4Changed();
    emit controllerMappingChanged();
    emit packetLossReportedMaxChanged();
    emit currentProfileChanged();
    emit profilesChanged();
    emit gyroSteeringChanged();
    refreshAllPlaceboKeys();
}

void QmlSettings::refreshAllPlaceboKeys()
{
    emit directFrameMappingChanged();
    emit placeboUpscalerChanged();
    emit placeboPlaneUpscalerChanged();
    emit placeboDownscalerChanged();
    emit placeboPlaneDownscalerChanged();
    emit placeboFrameMixerChanged();
    emit placeboDeinterlaceEnabledChanged();
    emit placeboDeinterlacePresetChanged();
    emit placeboDeinterlaceAlgorithmChanged();
    emit placeboDeinterlaceSkipSpatialChanged();
    emit placeboAntiringingStrengthChanged();
    emit placeboDebandEnabledChanged();
    emit placeboDebandPresetChanged();
    emit placeboDebandIterationsChanged();
    emit placeboDebandThresholdChanged();
    emit placeboDebandRadiusChanged();
    emit placeboDebandGrainChanged();
    emit placeboSigmoidEnabledChanged();
    emit placeboSigmoidPresetChanged();
    emit placeboSigmoidCenterChanged();
    emit placeboSigmoidSlopeChanged();
    emit placeboColorAdjustmentEnabledChanged();
    emit placeboColorAdjustmentPresetChanged();
    emit placeboColorAdjustmentBrightnessChanged();
    emit placeboColorAdjustmentContrastChanged();
    emit placeboColorAdjustmentSaturationChanged();
    emit placeboColorAdjustmentHueChanged();
    emit placeboColorAdjustmentGammaChanged();
    emit placeboColorAdjustmentTemperatureChanged();
    emit placeboPeakDetectionEnabled();
    emit placeboPeakDetectionPresetChanged();
    emit placeboPeakDetectionPeakSmoothingPeriodChanged();
    emit placeboPeakDetectionSceneThresholdLowChanged();
    emit placeboPeakDetectionSceneThresholdHighChanged();
    emit placeboPeakDetectionPeakPercentileChanged();
    emit placeboPeakDetectionBlackCutoffChanged();
    emit placeboPeakDetectionAllowDelayedPeakChanged();
    emit placeboColorMappingEnabledChanged();
    emit placeboColorMappingPresetChanged();
    emit placeboGamutMappingFunctionChanged();
    emit placeboGamutMappingPerceptualDeadzoneChanged();
    emit placeboGamutMappingPerceptualStrengthChanged();
    emit placeboGamutMappingColorimetricGammaChanged();
    emit placeboGamutMappingSoftClipKneeChanged();
    emit placeboGamutMappingSoftClipDesatChanged();
    emit placeboGamutMappingLut3dSizeIChanged();
    emit placeboGamutMappingLut3dSizeCChanged();
    emit placeboGamutMappingLut3dSizeHChanged();
    emit placeboGamutMappingLut3dTricubicEnabledChanged();
    emit placeboGamutMappingGamutExpansionEnabledChanged();
    emit placeboToneMappingFunctionChanged();
    emit placeboToneMappingKneeAdaptationChanged();
    emit placeboToneMappingKneeMinimumChanged();
    emit placeboToneMappingKneeMaximumChanged();
    emit placeboToneMappingKneeDefaultChanged();
    emit placeboToneMappingKneeOffsetChanged();
    emit placeboToneMappingSlopeTuningChanged();
    emit placeboToneMappingSlopeOffsetChanged();
    emit placeboToneMappingSplineContrastChanged();
    emit placeboToneMappingReinhardContrastChanged();
    emit placeboToneMappingLinearKneeChanged();
    emit placeboToneMappingExposureChanged();
    emit placeboToneMappingInverseToneMappingEnabledChanged();
    emit placeboToneMappingMetadataChanged();
    emit placeboToneMappingToneLutSizeChanged();
    emit placeboToneMappingContrastRecoveryChanged();
    emit placeboChanged();
}

void QmlSettings::exportSettings()
{
    QString profile = settings->GetCurrentProfile();
    if(profile.isEmpty())
        profile = "Default";
    QString fileName = QFileDialog::getSaveFileName(QApplication::focusWidget(), tr("Export %1 Profile To File").arg(profile),
                                                    QStandardPaths::writableLocation(QStandardPaths::DesktopLocation) + "/chiaki-ng-" + profile,
                                                    tr("Settings files (*.ini)"),
                                                    nullptr,
                                                    QFileDialog::DontUseNativeDialog | QFileDialog::DontConfirmOverwrite);
    if(fileName.isEmpty())
        return;
    settings->ExportSettings(std::move(fileName));
}

void QmlSettings::importSettings()
{
    QString fileName = QFileDialog::getOpenFileName(QApplication::focusWidget(), tr("Import Profile From File"),
                                                    QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
                                                    tr("Settings files (*.ini)"),
                                                    nullptr,
                                                    QFileDialog::DontUseNativeDialog);
    if(fileName.isEmpty())
        return;
    settings->ImportSettings(std::move(fileName));
    refreshAllKeys();
}

QString QmlSettings::chooseSteamBasePath()
{
    QString fileName = QFileDialog::getExistingDirectory(QApplication::focusWidget(), tr("Choose Steam Base Path"),
                                                    QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
                                                    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks | QFileDialog::DontUseNativeDialog);
    printf("Chosen filename: %s", fileName.toUtf8().constData());
    return fileName;
}

void QmlSettings::exportPlaceboSettings()
{
    QString fileName = QFileDialog::getSaveFileName(QApplication::focusWidget(), tr("Export Placebo Renderer Settings To File"),
                                                    QStandardPaths::writableLocation(QStandardPaths::DesktopLocation) + "/chiaki-ng-placebo",
                                                    tr("Settings files (*.ini)"),
                                                    nullptr,
                                                    QFileDialog::DontUseNativeDialog | QFileDialog::DontConfirmOverwrite);
    if(fileName.isEmpty())
        return;
    settings->ExportPlaceboSettings(std::move(fileName));
}

void QmlSettings::importPlaceboSettings()
{
    QString fileName = QFileDialog::getOpenFileName(QApplication::focusWidget(), tr("Import Placebo Renderer Settings From File"),
                                                    QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
                                                    tr("Settings files (*.ini)"),
                                                    nullptr,
                                                    QFileDialog::DontUseNativeDialog);
    if(fileName.isEmpty())
        return;
    settings->ImportPlaceboSettings(std::move(fileName));;
    refreshAllPlaceboKeys();
}
