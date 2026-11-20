#pragma once

#include "settings.h"

class QmlSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool remotePlayAsk READ remotePlayAsk WRITE setRemotePlayAsk NOTIFY remotePlayAskChanged)
    Q_PROPERTY(bool addSteamShortcutAsk READ addSteamShortcutAsk WRITE setAddSteamShortcutAsk NOTIFY addSteamShortcutAskChanged)
    Q_PROPERTY(bool hideCursor READ hideCursor WRITE setHideCursor NOTIFY hideCursorChanged)
    Q_PROPERTY(int audioVideoDisabled READ audioVideoDisabled WRITE setAudioVideoDisabled NOTIFY audioVideoDisabledChanged)
    Q_PROPERTY(int resolutionLocalPS4 READ resolutionLocalPS4 WRITE setResolutionLocalPS4 NOTIFY resolutionLocalPS4Changed)
    Q_PROPERTY(int resolutionRemotePS4 READ resolutionRemotePS4 WRITE setResolutionRemotePS4 NOTIFY resolutionRemotePS4Changed)
    Q_PROPERTY(int resolutionLocalPS5 READ resolutionLocalPS5 WRITE setResolutionLocalPS5 NOTIFY resolutionLocalPS5Changed)
    Q_PROPERTY(int resolutionRemotePS5 READ resolutionRemotePS5 WRITE setResolutionRemotePS5 NOTIFY resolutionRemotePS5Changed)
    Q_PROPERTY(int disconnectAction READ disconnectAction WRITE setDisconnectAction NOTIFY disconnectActionChanged)
    Q_PROPERTY(int suspendAction READ suspendAction WRITE setSuspendAction NOTIFY suspendActionChanged)
    Q_PROPERTY(bool logVerbose READ logVerbose WRITE setLogVerbose NOTIFY logVerboseChanged)
    Q_PROPERTY(bool logSanitize READ logSanitize WRITE setLogSanitize NOTIFY logSanitizeChanged)
    Q_PROPERTY(bool vSyncEnabled READ vSyncEnabled WRITE setVSyncEnabled NOTIFY vSyncEnabledChanged)
    Q_PROPERTY(int rumbleHapticsIntensity READ rumbleHapticsIntensity WRITE setRumbleHapticsIntensity NOTIFY rumbleHapticsIntensityChanged)
#ifdef CHIAKI_GUI_ENABLE_STEAMDECK_NATIVE
    Q_PROPERTY(bool steamDeckHaptics READ steamDeckHaptics WRITE setSteamDeckHaptics NOTIFY steamDeckHapticsChanged)
    Q_PROPERTY(bool verticalDeck READ verticalDeck WRITE setVerticalDeck NOTIFY verticalDeckChanged)
#endif
    Q_PROPERTY(bool buttonsByPosition READ buttonsByPosition WRITE setButtonsByPosition NOTIFY buttonsByPositionChanged)
    Q_PROPERTY(bool startMicUnmuted READ startMicUnmuted WRITE setStartMicUnmuted NOTIFY startMicUnmutedChanged)
#ifdef CHIAKI_GUI_ENABLE_SPEEX
    Q_PROPERTY(bool speechProcessing READ speechProcessing WRITE setSpeechProcessing NOTIFY speechProcessingChanged)
    Q_PROPERTY(int noiseSuppressLevel READ noiseSuppressLevel WRITE setNoiseSuppressLevel NOTIFY noiseSuppressLevelChanged)
    Q_PROPERTY(int echoSuppressLevel READ echoSuppressLevel WRITE setEchoSuppressLevel NOTIFY echoSuppressLevelChanged)
#endif
    Q_PROPERTY(bool showStreamStats READ showStreamStats WRITE setShowStreamStats NOTIFY showStreamStatsChanged)
    Q_PROPERTY(bool streamerMode READ streamerMode WRITE setStreamerMode NOTIFY streamerModeChanged)
    Q_PROPERTY(float hapticOverride READ hapticOverride WRITE setHapticOverride NOTIFY hapticOverrideChanged)
    Q_PROPERTY(int displayTargetContrast READ displayTargetContrast WRITE setDisplayTargetContrast NOTIFY displayTargetContrastChanged)
    Q_PROPERTY(int displayTargetPeak READ displayTargetPeak WRITE setDisplayTargetPeak NOTIFY displayTargetPeakChanged)
    Q_PROPERTY(int displayTargetPrim READ displayTargetPrim WRITE setDisplayTargetPrim NOTIFY displayTargetPrimChanged)
    Q_PROPERTY(int displayTargetTrc READ displayTargetTrc WRITE setDisplayTargetTrc NOTIFY displayTargetTrcChanged)
    Q_PROPERTY(bool fullscreenDoubleClick READ fullscreenDoubleClick WRITE setFullscreenDoubleClick NOTIFY fullscreenDoubleClickChanged)
    Q_PROPERTY(int fpsLocalPS4 READ fpsLocalPS4 WRITE setFpsLocalPS4 NOTIFY fpsLocalPS4Changed)
    Q_PROPERTY(int fpsRemotePS4 READ fpsRemotePS4 WRITE setFpsRemotePS4 NOTIFY fpsRemotePS4Changed)
    Q_PROPERTY(int fpsLocalPS5 READ fpsLocalPS5 WRITE setFpsLocalPS5 NOTIFY fpsLocalPS5Changed)
    Q_PROPERTY(int fpsRemotePS5 READ fpsRemotePS5 WRITE setFpsRemotePS5 NOTIFY fpsRemotePS5Changed)
    Q_PROPERTY(int bitrateLocalPS4 READ bitrateLocalPS4 WRITE setBitrateLocalPS4 NOTIFY bitrateLocalPS4Changed)
    Q_PROPERTY(int bitrateRemotePS4 READ bitrateRemotePS4 WRITE setBitrateRemotePS4 NOTIFY bitrateRemotePS4Changed)
    Q_PROPERTY(int bitrateLocalPS5 READ bitrateLocalPS5 WRITE setBitrateLocalPS5 NOTIFY bitrateLocalPS5Changed)
    Q_PROPERTY(int bitrateRemotePS5 READ bitrateRemotePS5 WRITE setBitrateRemotePS5 NOTIFY bitrateRemotePS5Changed)
    Q_PROPERTY(int codecLocalPS5 READ codecLocalPS5 WRITE setCodecLocalPS5 NOTIFY codecLocalPS5Changed)
    Q_PROPERTY(int codecRemotePS5 READ codecRemotePS5 WRITE setCodecRemotePS5 NOTIFY codecRemotePS5Changed)
    Q_PROPERTY(int audioBufferSize READ audioBufferSize WRITE setAudioBufferSize NOTIFY audioBufferSizeChanged)
    Q_PROPERTY(int audioVolume READ audioVolume WRITE setAudioVolume NOTIFY audioVolumeChanged)
    Q_PROPERTY(QString audioInDevice READ audioInDevice WRITE setAudioInDevice NOTIFY audioInDeviceChanged)
    Q_PROPERTY(QString audioOutDevice READ audioOutDevice WRITE setAudioOutDevice NOTIFY audioOutDeviceChanged)
    Q_PROPERTY(QString decoder READ decoder WRITE setDecoder NOTIFY decoderChanged)
    Q_PROPERTY(bool useZeroCopy READ useZeroCopy WRITE setUseZeroCopy NOTIFY useZeroCopyChanged)
    Q_PROPERTY(bool vulkanDeferredSwap READ vulkanDeferredSwap WRITE setVulkanDeferredSwap NOTIFY vulkanDeferredSwapChanged)
    Q_PROPERTY(int windowType READ windowType WRITE setWindowType NOTIFY windowTypeChanged)
    Q_PROPERTY(uint customResolutionWidth READ customResolutionWidth WRITE setCustomResolutionWidth NOTIFY customResolutionWidthChanged)
    Q_PROPERTY(uint customResolutionHeight READ customResolutionHeight WRITE setCustomResolutionHeight NOTIFY customResolutionHeightChanged)
    Q_PROPERTY(int videoPreset READ videoPreset WRITE setVideoPreset NOTIFY videoPresetChanged)
    Q_PROPERTY(int rendererBackend READ rendererBackend WRITE setRendererBackend NOTIFY rendererBackendChanged)
    Q_PROPERTY(float sZoomFactor READ sZoomFactor WRITE setSZoomFactor NOTIFY sZoomFactorChanged)
    Q_PROPERTY(int packetLossReportedMax READ packetLossReportedMax WRITE setPacketLossReportedMax NOTIFY packetLossReportedMaxChanged)
    Q_PROPERTY(QString autoConnectMac READ autoConnectMac WRITE setAutoConnectMac NOTIFY autoConnectMacChanged)
    Q_PROPERTY(bool allowJoystickBackgroundEvents READ allowJoystickBackgroundEvents WRITE setAllowJoystickBackgroundEvents NOTIFY allowJoystickBackgroundEventsChanged)
    Q_PROPERTY(QString logDirectory READ logDirectory CONSTANT)
    Q_PROPERTY(QStringList availableDecoders READ availableDecoders CONSTANT)
    Q_PROPERTY(QStringList availableAudioInDevices READ availableAudioInDevices NOTIFY audioDevicesChanged)
    Q_PROPERTY(QStringList availableAudioOutDevices READ availableAudioOutDevices NOTIFY audioDevicesChanged)
    Q_PROPERTY(QVariantList registeredHosts READ registeredHosts NOTIFY registeredHostsChanged)
    Q_PROPERTY(QStringList profiles READ profiles NOTIFY profilesChanged)
    Q_PROPERTY(QString currentProfile READ currentProfile WRITE setCurrentProfile NOTIFY currentProfileChanged)
    Q_PROPERTY(QVariantList controllerMapping READ controllerMapping NOTIFY controllerMappingChanged)
    Q_PROPERTY(uint wifiDroppedNotif READ wifiDroppedNotif WRITE setWifiDroppedNotif NOTIFY wifiDroppedNotifChanged)
    Q_PROPERTY(bool portGuessingEnabled READ portGuessingEnabled WRITE setPortGuessingEnabled NOTIFY portGuessingEnabledChanged)
    Q_PROPERTY(int portGuessCount READ portGuessCount WRITE setPortGuessCount NOTIFY portGuessCountChanged)
    Q_PROPERTY(int portGuessSocketCount READ portGuessSocketCount WRITE setPortGuessSocketCount NOTIFY portGuessSocketCountChanged)
    Q_PROPERTY(QString psnRefreshToken READ psnRefreshToken WRITE setPsnRefreshToken NOTIFY psnRefreshTokenChanged)
    Q_PROPERTY(QString psnAuthToken READ psnAuthToken WRITE setPsnAuthToken NOTIFY psnAuthTokenChanged)
    Q_PROPERTY(QString psnAuthTokenExpiry READ psnAuthTokenExpiry WRITE setPsnAuthTokenExpiry NOTIFY psnAuthTokenExpiryChanged)
    Q_PROPERTY(QString psnAccountId READ psnAccountId WRITE setPsnAccountId NOTIFY psnAccountIdChanged)
    Q_PROPERTY(bool mouseTouchEnabled READ mouseTouchEnabled WRITE setMouseTouchEnabled NOTIFY mouseTouchEnabledChanged)
    Q_PROPERTY(bool keyboardEnabled READ keyboardEnabled WRITE setKeyboardEnabled NOTIFY keyboardEnabledChanged)
    Q_PROPERTY(bool dpadTouchEnabled READ dpadTouchEnabled WRITE setDpadTouchEnabled NOTIFY dpadTouchEnabledChanged)
    Q_PROPERTY(bool iDROnFECFailureEnabled READ iDROnFECFailureEnabled WRITE setIDROnFECFailureEnabled NOTIFY iDROnFECFailureEnabledChanged)
    Q_PROPERTY(uint16_t dpadTouchIncrement READ dpadTouchIncrement WRITE setDpadTouchIncrement NOTIFY dpadTouchIncrementChanged)
    Q_PROPERTY(uint dpadTouchShortcut1 READ dpadTouchShortcut1 WRITE setDpadTouchShortcut1 NOTIFY dpadTouchShortcut1Changed)
    Q_PROPERTY(uint dpadTouchShortcut2 READ dpadTouchShortcut2 WRITE setDpadTouchShortcut2 NOTIFY dpadTouchShortcut2Changed)
    Q_PROPERTY(uint dpadTouchShortcut3 READ dpadTouchShortcut3 WRITE setDpadTouchShortcut3 NOTIFY dpadTouchShortcut3Changed)
    Q_PROPERTY(uint dpadTouchShortcut4 READ dpadTouchShortcut4 WRITE setDpadTouchShortcut4 NOTIFY dpadTouchShortcut4Changed)
    Q_PROPERTY(bool streamMenuEnabled READ streamMenuEnabled WRITE setStreamMenuEnabled NOTIFY streamMenuEnabledChanged)
    Q_PROPERTY(uint streamMenuShortcut1 READ streamMenuShortcut1 WRITE setStreamMenuShortcut1 NOTIFY streamMenuShortcut1Changed)
    Q_PROPERTY(uint streamMenuShortcut2 READ streamMenuShortcut2 WRITE setStreamMenuShortcut2 NOTIFY streamMenuShortcut2Changed)
    Q_PROPERTY(uint streamMenuShortcut3 READ streamMenuShortcut3 WRITE setStreamMenuShortcut3 NOTIFY streamMenuShortcut3Changed)
    Q_PROPERTY(uint streamMenuShortcut4 READ streamMenuShortcut4 WRITE setStreamMenuShortcut4 NOTIFY streamMenuShortcut4Changed)
    Q_PROPERTY(int placeboUpscaler READ placeboUpscaler WRITE setPlaceboUpscaler NOTIFY placeboUpscalerChanged)
    Q_PROPERTY(int placeboPlaneUpscaler READ placeboPlaneUpscaler WRITE setPlaceboPlaneUpscaler NOTIFY placeboPlaneUpscalerChanged)
	Q_PROPERTY(int placeboDownscaler READ placeboDownscaler WRITE setPlaceboDownscaler NOTIFY placeboDownscalerChanged)
	Q_PROPERTY(int placeboPlaneDownscaler READ placeboPlaneDownscaler WRITE setPlaceboPlaneDownscaler NOTIFY placeboPlaneDownscalerChanged)
	Q_PROPERTY(int placeboFrameMixer READ placeboFrameMixer WRITE setPlaceboFrameMixer NOTIFY placeboFrameMixerChanged)
	Q_PROPERTY(bool directFrameMapping READ directFrameMapping WRITE setDirectFrameMapping NOTIFY directFrameMappingChanged)
	Q_PROPERTY(bool placeboDeinterlaceEnabled READ placeboDeinterlaceEnabled WRITE setPlaceboDeinterlaceEnabled NOTIFY placeboDeinterlaceEnabledChanged)
	Q_PROPERTY(int placeboDeinterlacePreset READ placeboDeinterlacePreset WRITE setPlaceboDeinterlacePreset NOTIFY placeboDeinterlacePresetChanged)
	Q_PROPERTY(int placeboDeinterlaceAlgorithm READ placeboDeinterlaceAlgorithm WRITE setPlaceboDeinterlaceAlgorithm NOTIFY placeboDeinterlaceAlgorithmChanged)
	Q_PROPERTY(bool placeboDeinterlaceSkipSpatial READ placeboDeinterlaceSkipSpatial WRITE setPlaceboDeinterlaceSkipSpatial NOTIFY placeboDeinterlaceSkipSpatialChanged)
	Q_PROPERTY(float placeboAntiringingStrength READ placeboAntiringingStrength WRITE setPlaceboAntiringingStrength NOTIFY placeboAntiringingStrengthChanged)
    Q_PROPERTY(bool placeboDebandEnabled READ placeboDebandEnabled WRITE setPlaceboDebandEnabled NOTIFY placeboDebandEnabledChanged)
    Q_PROPERTY(int placeboDebandPreset READ placeboDebandPreset WRITE setPlaceboDebandPreset NOTIFY placeboDebandPresetChanged)
    Q_PROPERTY(int placeboDebandIterations READ placeboDebandIterations WRITE setPlaceboDebandIterations NOTIFY placeboDebandIterationsChanged)
    Q_PROPERTY(float placeboDebandThreshold READ placeboDebandThreshold WRITE setPlaceboDebandThreshold NOTIFY placeboDebandThresholdChanged)
    Q_PROPERTY(float placeboDebandRadius READ placeboDebandRadius WRITE setPlaceboDebandRadius NOTIFY placeboDebandRadiusChanged)
    Q_PROPERTY(float placeboDebandGrain READ placeboDebandGrain WRITE setPlaceboDebandGrain NOTIFY placeboDebandGrainChanged)
    Q_PROPERTY(bool placeboSigmoidEnabled READ placeboSigmoidEnabled WRITE setPlaceboSigmoidEnabled NOTIFY placeboSigmoidEnabledChanged)
    Q_PROPERTY(int placeboSigmoidPreset READ placeboSigmoidPreset WRITE setPlaceboSigmoidPreset NOTIFY placeboSigmoidPresetChanged)
    Q_PROPERTY(float placeboSigmoidCenter READ placeboSigmoidCenter WRITE setPlaceboSigmoidCenter NOTIFY placeboSigmoidCenterChanged)
    Q_PROPERTY(float placeboSigmoidSlope READ placeboSigmoidSlope WRITE setPlaceboSigmoidSlope NOTIFY placeboSigmoidSlopeChanged)
    Q_PROPERTY(bool placeboColorAdjustmentEnabled READ placeboColorAdjustmentEnabled WRITE setPlaceboColorAdjustmentEnabled NOTIFY placeboColorAdjustmentEnabledChanged)
    Q_PROPERTY(int placeboColorAdjustmentPreset READ placeboColorAdjustmentPreset WRITE setPlaceboColorAdjustmentPreset NOTIFY placeboColorAdjustmentPresetChanged)
    Q_PROPERTY(float placeboColorAdjustmentBrightness READ placeboColorAdjustmentBrightness WRITE setPlaceboColorAdjustmentBrightness NOTIFY placeboColorAdjustmentBrightnessChanged)
    Q_PROPERTY(float placeboColorAdjustmentContrast READ placeboColorAdjustmentContrast WRITE setPlaceboColorAdjustmentContrast NOTIFY placeboColorAdjustmentContrastChanged)
    Q_PROPERTY(float placeboColorAdjustmentSaturation READ placeboColorAdjustmentSaturation WRITE setPlaceboColorAdjustmentSaturation NOTIFY placeboColorAdjustmentSaturationChanged)
    Q_PROPERTY(float placeboColorAdjustmentHue READ placeboColorAdjustmentHue WRITE setPlaceboColorAdjustmentHue NOTIFY placeboColorAdjustmentHueChanged)
    Q_PROPERTY(float placeboColorAdjustmentGamma READ placeboColorAdjustmentGamma WRITE setPlaceboColorAdjustmentGamma NOTIFY placeboColorAdjustmentGammaChanged)
    Q_PROPERTY(float placeboColorAdjustmentTemperature READ placeboColorAdjustmentTemperature WRITE setPlaceboColorAdjustmentTemperature NOTIFY placeboColorAdjustmentTemperatureChanged)
    Q_PROPERTY(bool placeboPeakDetectionEnabled READ placeboPeakDetectionEnabled WRITE setPlaceboPeakDetectionEnabled NOTIFY placeboPeakDetectionEnabledChanged)
    Q_PROPERTY(int placeboPeakDetectionPreset READ placeboPeakDetectionPreset WRITE setPlaceboPeakDetectionPreset NOTIFY placeboPeakDetectionPresetChanged)
    Q_PROPERTY(float placeboPeakDetectionPeakSmoothingPeriod READ placeboPeakDetectionPeakSmoothingPeriod WRITE setPlaceboPeakDetectionPeakSmoothingPeriod NOTIFY placeboPeakDetectionPeakSmoothingPeriodChanged)
    Q_PROPERTY(float placeboPeakDetectionSceneThresholdLow READ placeboPeakDetectionSceneThresholdLow WRITE setPlaceboPeakDetectionSceneThresholdLow NOTIFY placeboPeakDetectionSceneThresholdLowChanged)
    Q_PROPERTY(float placeboPeakDetectionSceneThresholdHigh READ placeboPeakDetectionSceneThresholdHigh WRITE setPlaceboPeakDetectionSceneThresholdHigh NOTIFY placeboPeakDetectionSceneThresholdHighChanged)
    Q_PROPERTY(float placeboPeakDetectionPeakPercentile READ placeboPeakDetectionPeakPercentile WRITE setPlaceboPeakDetectionPeakPercentile NOTIFY placeboPeakDetectionPeakPercentileChanged)
    Q_PROPERTY(float placeboPeakDetectionBlackCutoff READ placeboPeakDetectionBlackCutoff WRITE setPlaceboPeakDetectionBlackCutoff NOTIFY placeboPeakDetectionBlackCutoffChanged)
    Q_PROPERTY(bool placeboPeakDetectionAllowDelayedPeak READ placeboPeakDetectionAllowDelayedPeak WRITE setPlaceboPeakDetectionAllowDelayedPeak NOTIFY placeboPeakDetectionAllowDelayedPeakChanged)
    Q_PROPERTY(bool placeboColorMappingEnabled READ placeboColorMappingEnabled WRITE setPlaceboColorMappingEnabled NOTIFY placeboColorMappingEnabledChanged)
    Q_PROPERTY(int placeboColorMappingPreset READ placeboColorMappingPreset WRITE setPlaceboColorMappingPreset NOTIFY placeboColorMappingPresetChanged)
    Q_PROPERTY(int placeboGamutMappingFunction READ placeboGamutMappingFunction WRITE setPlaceboGamutMappingFunction NOTIFY placeboGamutMappingFunctionChanged)
    Q_PROPERTY(float placeboGamutMappingPerceptualDeadzone READ placeboGamutMappingPerceptualDeadzone WRITE setPlaceboGamutMappingPerceptualDeadzone NOTIFY placeboGamutMappingPerceptualDeadzoneChanged)
    Q_PROPERTY(float placeboGamutMappingPerceptualStrength READ placeboGamutMappingPerceptualStrength WRITE setPlaceboGamutMappingPerceptualStrength NOTIFY placeboGamutMappingPerceptualStrengthChanged)
    Q_PROPERTY(float placeboGamutMappingColorimetricGamma READ placeboGamutMappingColorimetricGamma WRITE setPlaceboGamutMappingColorimetricGamma NOTIFY placeboGamutMappingColorimetricGammaChanged)
    Q_PROPERTY(float placeboGamutMappingSoftClipKnee READ placeboGamutMappingSoftClipKnee WRITE setPlaceboGamutMappingSoftClipKnee NOTIFY placeboGamutMappingSoftClipKneeChanged)
    Q_PROPERTY(float placeboGamutMappingSoftClipDesat READ placeboGamutMappingSoftClipDesat WRITE setPlaceboGamutMappingSoftClipDesat NOTIFY placeboGamutMappingSoftClipDesatChanged)
    Q_PROPERTY(int placeboGamutMappingLut3dSizeI READ placeboGamutMappingLut3dSizeI WRITE setPlaceboGamutMappingLut3dSizeI NOTIFY placeboGamutMappingLut3dSizeIChanged)
    Q_PROPERTY(int placeboGamutMappingLut3dSizeC READ placeboGamutMappingLut3dSizeC WRITE setPlaceboGamutMappingLut3dSizeC NOTIFY placeboGamutMappingLut3dSizeCChanged)
    Q_PROPERTY(int placeboGamutMappingLut3dSizeH READ placeboGamutMappingLut3dSizeH WRITE setPlaceboGamutMappingLut3dSizeH NOTIFY placeboGamutMappingLut3dSizeHChanged)
    Q_PROPERTY(bool placeboGamutMappingLut3dTricubicEnabled READ placeboGamutMappingLut3dTricubicEnabled WRITE setPlaceboGamutMappingLut3dTricubicEnabled NOTIFY placeboGamutMappingLut3dTricubicEnabledChanged)
    Q_PROPERTY(bool placeboGamutMappingGamutExpansionEnabled READ placeboGamutMappingGamutExpansionEnabled WRITE setPlaceboGamutMappingGamutExpansionEnabled NOTIFY placeboGamutMappingGamutExpansionEnabledChanged)
    Q_PROPERTY(int placeboToneMappingFunction READ placeboToneMappingFunction WRITE setPlaceboToneMappingFunction NOTIFY placeboToneMappingFunctionChanged)
    Q_PROPERTY(float placeboToneMappingKneeAdaptation READ placeboToneMappingKneeAdaptation WRITE setPlaceboToneMappingKneeAdaptation NOTIFY placeboToneMappingKneeAdaptationChanged)
    Q_PROPERTY(float placeboToneMappingKneeMinimum READ placeboToneMappingKneeMinimum WRITE setPlaceboToneMappingKneeMinimum NOTIFY placeboToneMappingKneeMinimumChanged)
    Q_PROPERTY(float placeboToneMappingKneeMaximum READ placeboToneMappingKneeMaximum WRITE setPlaceboToneMappingKneeMaximum NOTIFY placeboToneMappingKneeMaximumChanged)
    Q_PROPERTY(float placeboToneMappingKneeDefault READ placeboToneMappingKneeDefault WRITE setPlaceboToneMappingKneeDefault NOTIFY placeboToneMappingKneeDefaultChanged)
    Q_PROPERTY(float placeboToneMappingKneeOffset READ placeboToneMappingKneeOffset WRITE setPlaceboToneMappingKneeOffset NOTIFY placeboToneMappingKneeOffsetChanged)
    Q_PROPERTY(float placeboToneMappingSlopeTuning READ placeboToneMappingSlopeTuning WRITE setPlaceboToneMappingSlopeTuning NOTIFY placeboToneMappingSlopeTuningChanged)
    Q_PROPERTY(float placeboToneMappingSlopeOffset READ placeboToneMappingSlopeOffset WRITE setPlaceboToneMappingSlopeOffset NOTIFY placeboToneMappingSlopeOffsetChanged)
    Q_PROPERTY(float placeboToneMappingSplineContrast READ placeboToneMappingSplineContrast WRITE setPlaceboToneMappingSplineContrast NOTIFY placeboToneMappingSplineContrastChanged)
    Q_PROPERTY(float placeboToneMappingReinhardContrast READ placeboToneMappingReinhardContrast WRITE setPlaceboToneMappingReinhardContrast NOTIFY placeboToneMappingReinhardContrastChanged)
    Q_PROPERTY(float placeboToneMappingLinearKnee READ placeboToneMappingLinearKnee WRITE setPlaceboToneMappingLinearKnee NOTIFY placeboToneMappingLinearKneeChanged)
    Q_PROPERTY(float placeboToneMappingExposure READ placeboToneMappingExposure WRITE setPlaceboToneMappingExposure NOTIFY placeboToneMappingExposureChanged)
    Q_PROPERTY(bool placeboToneMappingInverseToneMappingEnabled READ placeboToneMappingInverseToneMappingEnabled WRITE setPlaceboToneMappingInverseToneMappingEnabled NOTIFY placeboToneMappingInverseToneMappingEnabledChanged)
    Q_PROPERTY(int placeboToneMappingMetadata READ placeboToneMappingMetadata WRITE setPlaceboToneMappingMetadata NOTIFY placeboToneMappingMetadataChanged)
    Q_PROPERTY(int placeboToneMappingToneLutSize READ placeboToneMappingToneLutSize WRITE setPlaceboToneMappingToneLutSize NOTIFY placeboToneMappingToneLutSizeChanged)
    Q_PROPERTY(float placeboToneMappingContrastRecovery READ placeboToneMappingContrastRecovery WRITE setPlaceboToneMappingContrastRecovery NOTIFY placeboToneMappingContrastRecoveryChanged)
    Q_PROPERTY(float placeboToneMappingContrastSmoothness READ placeboToneMappingContrastSmoothness WRITE setPlaceboToneMappingContrastSmoothness NOTIFY placeboToneMappingContrastSmoothnessChanged)

public:
    QmlSettings(Settings *settings, QObject *parent = nullptr);

    int resolutionLocalPS4() const;
    void setResolutionLocalPS4(int resolution);
    int resolutionRemotePS4() const;
    void setResolutionRemotePS4(int resolution);
    int resolutionLocalPS5() const;
    void setResolutionLocalPS5(int resolution);
    int resolutionRemotePS5() const;
    void setResolutionRemotePS5(int resolution);

    int audioVideoDisabled() const;
    void setAudioVideoDisabled(int disabled);

    bool showStreamStats() const;
    void setShowStreamStats(bool enabled);

    bool streamerMode() const;
    void setStreamerMode(bool enabled);

    int displayTargetContrast() const;
    void setDisplayTargetContrast(int contrast);

    int displayTargetPeak() const;
    void setDisplayTargetPeak(int peak);

    int displayTargetPrim() const;
    void setDisplayTargetPrim(int prim);

    int displayTargetTrc() const;
    void setDisplayTargetTrc(int trc);

    int disconnectAction() const;
    void setDisconnectAction(int action);

    int suspendAction() const;
    void setSuspendAction(int action);

    bool logVerbose() const;
    void setLogVerbose(bool verbose);
    bool logSanitize() const;
    void setLogSanitize(bool enabled);
    bool vSyncEnabled() const;
    void setVSyncEnabled(bool enabled);

    int rumbleHapticsIntensity() const;
    void setRumbleHapticsIntensity(int intensity);

#ifdef CHIAKI_GUI_ENABLE_STEAMDECK_NATIVE
    bool steamDeckHaptics() const;
    void setSteamDeckHaptics(bool steamDeckHaptics);

    bool verticalDeck() const;
    void setVerticalDeck(bool vertical);
#endif

    bool buttonsByPosition() const;
    void setButtonsByPosition(bool buttonsByPosition);

    bool iDROnFECFailureEnabled() const;
    void setIDROnFECFailureEnabled(bool enabled);

    bool allowJoystickBackgroundEvents() const;
    void setAllowJoystickBackgroundEvents(bool allowJoystickBackgroundEvents);

    bool startMicUnmuted() const;
    void setStartMicUnmuted(bool startMicUnmuted);

#ifdef CHIAKI_GUI_ENABLE_SPEEX
    bool speechProcessing() const;
    void setSpeechProcessing(bool processing);

    int noiseSuppressLevel() const;
    void setNoiseSuppressLevel(int level);

    int echoSuppressLevel() const;
    void setEchoSuppressLevel(int level);
#endif
    bool fullscreenDoubleClick() const;
    void setFullscreenDoubleClick(bool enabled);

    bool remotePlayAsk() const;
    void setRemotePlayAsk(bool asked);

    bool addSteamShortcutAsk() const;
    void setAddSteamShortcutAsk(bool asked);

    bool hideCursor() const;
    void setHideCursor(bool enabled);

    float hapticOverride() const;
    void setHapticOverride(float override);

    int fpsLocalPS4() const;
    void setFpsLocalPS4(int fps);
    int fpsRemotePS4() const;
    void setFpsRemotePS4(int fps);
    int fpsLocalPS5() const;
    void setFpsLocalPS5(int fps);
    int fpsRemotePS5() const;
    void setFpsRemotePS5(int fps);

    int bitrateLocalPS4() const;
    void setBitrateLocalPS4(int bitrate);
    int bitrateRemotePS4() const;
    void setBitrateRemotePS4(int bitrate);
    int bitrateLocalPS5() const;
    void setBitrateLocalPS5(int bitrate);
    int bitrateRemotePS5() const;
    void setBitrateRemotePS5(int bitrate);

    int codecLocalPS5() const;
    void setCodecLocalPS5(int codec);
    int codecRemotePS5() const;
    void setCodecRemotePS5(int codec);

    int audioBufferSize() const;
    void setAudioBufferSize(int size);

    int audioVolume() const;
    void setAudioVolume(int volume);

    QString audioInDevice() const;
    void setAudioInDevice(const QString &device);

    QString audioOutDevice() const;
    void setAudioOutDevice(const QString &device);

    QString decoder() const;
    void setDecoder(const QString &decoder);
    bool useZeroCopy() const;
    void setUseZeroCopy(bool enabled);
    bool vulkanDeferredSwap() const;
    void setVulkanDeferredSwap(bool enabled);

    int windowType() const;
    void setWindowType(int type);

    uint customResolutionWidth() const;
    void setCustomResolutionWidth(uint width);

    uint customResolutionHeight() const;
    void setCustomResolutionHeight(uint length);

    float sZoomFactor() const;
    void setSZoomFactor(float factor);

    int packetLossReportedMax() const;
    void setPacketLossReportedMax(int packet_loss_reported_max);

    int videoPreset() const;
    void setVideoPreset(int preset);

    int rendererBackend() const;
    void setRendererBackend(int backend);
    Q_INVOKABLE bool restartApplication();

    QString autoConnectMac() const;
    void setAutoConnectMac(const QString &mac);

    uint wifiDroppedNotif() const;
    void setWifiDroppedNotif(uint percent);
    bool portGuessingEnabled() const;
    void setPortGuessingEnabled(bool enabled);
    int portGuessCount() const;
    void setPortGuessCount(int count);
    int portGuessSocketCount() const;
    void setPortGuessSocketCount(int count);

    int placeboUpscaler() const;
    void setPlaceboUpscaler(int upscaler);

    int placeboPlaneUpscaler() const;
    void setPlaceboPlaneUpscaler(int upscaler);

    int placeboDownscaler() const;
    void setPlaceboDownscaler(int downscaler);

    int placeboPlaneDownscaler() const;
    void setPlaceboPlaneDownscaler(int downscaler);

    int placeboFrameMixer() const;
    bool directFrameMapping() const;
    void setDirectFrameMapping(bool enabled);
    void setPlaceboFrameMixer(int mixer);

    bool placeboDeinterlaceEnabled() const;
    void setPlaceboDeinterlaceEnabled(bool enabled);

    int placeboDeinterlacePreset() const;
    void setPlaceboDeinterlacePreset(int preset);

    int placeboDeinterlaceAlgorithm() const;
    void setPlaceboDeinterlaceAlgorithm(int algorithm);

    bool placeboDeinterlaceSkipSpatial() const;
    void setPlaceboDeinterlaceSkipSpatial(bool skip);

    float placeboAntiringingStrength() const;
    void setPlaceboAntiringingStrength(float strength);

    bool placeboDebandEnabled() const;
    void setPlaceboDebandEnabled(bool enabled);

    int placeboDebandPreset() const;
    void setPlaceboDebandPreset(int preset);

    int placeboDebandIterations() const;
    void setPlaceboDebandIterations(int iterations);

    float placeboDebandThreshold() const;
    void setPlaceboDebandThreshold(float threshold);

    float placeboDebandRadius() const;
    void setPlaceboDebandRadius(float radius);

    float placeboDebandGrain() const;
    void setPlaceboDebandGrain(float grain);

    bool placeboSigmoidEnabled() const;
    void setPlaceboSigmoidEnabled(bool enabled);

    int placeboSigmoidPreset() const;
    void setPlaceboSigmoidPreset(int preset);

    float placeboSigmoidCenter() const;
    void setPlaceboSigmoidCenter(float center);

    float placeboSigmoidSlope() const;
    void setPlaceboSigmoidSlope(float slope);

    bool placeboColorAdjustmentEnabled() const;
    void setPlaceboColorAdjustmentEnabled(bool enabled);

    int placeboColorAdjustmentPreset() const;
    void setPlaceboColorAdjustmentPreset(int preset);

    float placeboColorAdjustmentBrightness() const;
    void setPlaceboColorAdjustmentBrightness(float brightness);

    float placeboColorAdjustmentContrast() const;
    void setPlaceboColorAdjustmentContrast(float contrast);

    float placeboColorAdjustmentSaturation() const;
    void setPlaceboColorAdjustmentSaturation(float saturation);

    float placeboColorAdjustmentHue() const;
    void setPlaceboColorAdjustmentHue(float hue);

    float placeboColorAdjustmentGamma() const;
    void setPlaceboColorAdjustmentGamma(float gamma);

    float placeboColorAdjustmentTemperature() const;
    void setPlaceboColorAdjustmentTemperature(float temperature);

    bool placeboPeakDetectionEnabled() const;
    void setPlaceboPeakDetectionEnabled(bool enabled);

    int placeboPeakDetectionPreset() const;
    void setPlaceboPeakDetectionPreset(int preset);

    float placeboPeakDetectionPeakSmoothingPeriod() const;
    void setPlaceboPeakDetectionPeakSmoothingPeriod(float period);

    float placeboPeakDetectionSceneThresholdLow() const;
    void setPlaceboPeakDetectionSceneThresholdLow(float threshold_low);

    float placeboPeakDetectionSceneThresholdHigh() const;
    void setPlaceboPeakDetectionSceneThresholdHigh(float threshold_high);

    float placeboPeakDetectionPeakPercentile() const;
    void setPlaceboPeakDetectionPeakPercentile(float percentile);

    float placeboPeakDetectionBlackCutoff() const;
    void setPlaceboPeakDetectionBlackCutoff(float cutoff);

    bool placeboPeakDetectionAllowDelayedPeak() const;
    void setPlaceboPeakDetectionAllowDelayedPeak(bool allowed);

    bool placeboColorMappingEnabled() const;
    void setPlaceboColorMappingEnabled(bool enabled);

    int placeboColorMappingPreset() const;
    void setPlaceboColorMappingPreset(int preset);

    int placeboGamutMappingFunction() const;
    void setPlaceboGamutMappingFunction(int function);

    float placeboGamutMappingPerceptualDeadzone() const;
    void setPlaceboGamutMappingPerceptualDeadzone(float deadzone);

    float placeboGamutMappingPerceptualStrength() const;
    void setPlaceboGamutMappingPerceptualStrength(float strength);

    float placeboGamutMappingColorimetricGamma() const;
    void setPlaceboGamutMappingColorimetricGamma(float gamma);

    float placeboGamutMappingSoftClipKnee() const;
    void setPlaceboGamutMappingSoftClipKnee(float knee);

    float placeboGamutMappingSoftClipDesat() const;
    void setPlaceboGamutMappingSoftClipDesat(float desat);

    int placeboGamutMappingLut3dSizeI() const;
    void setPlaceboGamutMappingLut3dSizeI(int size);

    int placeboGamutMappingLut3dSizeC() const;
    void setPlaceboGamutMappingLut3dSizeC(int size);

    int placeboGamutMappingLut3dSizeH() const;
    void setPlaceboGamutMappingLut3dSizeH(int size);

    bool placeboGamutMappingLut3dTricubicEnabled() const;
    void setPlaceboGamutMappingLut3dTricubicEnabled(bool enabled);

    bool placeboGamutMappingGamutExpansionEnabled() const;
    void setPlaceboGamutMappingGamutExpansionEnabled(bool enabled);

    int placeboToneMappingFunction() const;
    void setPlaceboToneMappingFunction(int function);

    float placeboToneMappingKneeAdaptation() const;
    void setPlaceboToneMappingKneeAdaptation(float adaptation);

    float placeboToneMappingKneeMinimum() const;
    void setPlaceboToneMappingKneeMinimum(float min);

    float placeboToneMappingKneeMaximum() const;
    void setPlaceboToneMappingKneeMaximum(float max);

    float placeboToneMappingKneeDefault() const;
    void setPlaceboToneMappingKneeDefault(float default_knee);

    float placeboToneMappingKneeOffset() const;
    void setPlaceboToneMappingKneeOffset(float offset);

    float placeboToneMappingSlopeTuning() const;
    void setPlaceboToneMappingSlopeTuning(float tuning);

    float placeboToneMappingSlopeOffset() const;
    void setPlaceboToneMappingSlopeOffset(float offset);

    float placeboToneMappingSplineContrast() const;
    void setPlaceboToneMappingSplineContrast(float contrast);

    float placeboToneMappingReinhardContrast() const;
    void setPlaceboToneMappingReinhardContrast(float contrast);

    float placeboToneMappingLinearKnee() const;
    void setPlaceboToneMappingLinearKnee(float knee);

    float placeboToneMappingExposure() const;
    void setPlaceboToneMappingExposure(float exposure);

    bool placeboToneMappingInverseToneMappingEnabled() const;
    void setPlaceboToneMappingInverseToneMappingEnabled(bool enabled);

    int placeboToneMappingMetadata() const;
    void setPlaceboToneMappingMetadata(int metadata);

    int placeboToneMappingToneLutSize() const;
    void setPlaceboToneMappingToneLutSize(int size);

    float placeboToneMappingContrastRecovery() const;
    void setPlaceboToneMappingContrastRecovery(float recovery);

    float placeboToneMappingContrastSmoothness() const;
    void setPlaceboToneMappingContrastSmoothness(float smoothness);

    QString psnAuthToken() const;
    void setPsnAuthToken(const QString &auth_token);

    QString psnRefreshToken() const;
    void setPsnRefreshToken(const QString &refresh_token);

    QString psnAuthTokenExpiry() const;
    void setPsnAuthTokenExpiry(const QString &expiry);

    QString psnAccountId() const;
    void setPsnAccountId(const QString &account_id);

    bool mouseTouchEnabled() const;
    void setMouseTouchEnabled(bool enabled);

    bool keyboardEnabled() const;
    void setKeyboardEnabled(bool enabled);

    bool dpadTouchEnabled() const;
    void setDpadTouchEnabled(bool enabled);

    uint16_t dpadTouchIncrement() const;
    void setDpadTouchIncrement(uint16_t increment);

    uint dpadTouchShortcut1() const;
    void setDpadTouchShortcut1(uint button);

    uint dpadTouchShortcut2() const;
    void setDpadTouchShortcut2(uint button);

    uint dpadTouchShortcut3() const;
    void setDpadTouchShortcut3(uint button);

    uint dpadTouchShortcut4() const;
    void setDpadTouchShortcut4(uint button);

    bool streamMenuEnabled() const;
    void setStreamMenuEnabled(bool enabled);

    uint streamMenuShortcut1() const;
    void setStreamMenuShortcut1(uint button);

    uint streamMenuShortcut2() const;
    void setStreamMenuShortcut2(uint button);

    uint streamMenuShortcut3() const;
    void setStreamMenuShortcut3(uint button);

    uint streamMenuShortcut4() const;
    void setStreamMenuShortcut4(uint button);

    QString currentProfile() const;
    void setCurrentProfile(const QString &profile);

    void setSettings(Settings *new_settings);
    void refreshAllKeys();
    void refreshAllPlaceboKeys();

    QString logDirectory() const;
    QStringList availableDecoders() const;
    QStringList availableAudioOutDevices() const;
    QStringList availableAudioInDevices() const;
    QVariantList registeredHosts() const;
    QVariantList controllerMapping() const;
    QStringList profiles() const;

    Q_INVOKABLE void deleteRegisteredHost(int index);
    Q_INVOKABLE void refreshAudioDevices();
    Q_INVOKABLE QString changeControllerKey(int button, int key);
    Q_INVOKABLE void clearKeyMapping();
    Q_INVOKABLE void exportSettings();
    Q_INVOKABLE QString chooseSteamBasePath();
    Q_INVOKABLE void exportPlaceboSettings();
    Q_INVOKABLE void importSettings();
    Q_INVOKABLE void importPlaceboSettings();
    Q_INVOKABLE void deleteProfile(QString profile);
    Q_INVOKABLE QString stringForDpadShortcut() const;
    Q_INVOKABLE QString stringForStreamMenuShortcut() const;

signals:
    void resolutionLocalPS4Changed();
    void resolutionRemotePS4Changed();
    void resolutionLocalPS5Changed();
    void resolutionRemotePS5Changed();
    void disconnectActionChanged();
    void suspendActionChanged();
    void logVerboseChanged();
    void logSanitizeChanged();
    void vSyncEnabledChanged();
    void rumbleHapticsIntensityChanged();
    void buttonsByPositionChanged();
    void allowJoystickBackgroundEventsChanged();
    void startMicUnmutedChanged();
#ifdef CHIAKI_GUI_ENABLE_STEAMDECK_NATIVE
    void verticalDeckChanged();
    void steamDeckHapticsChanged();
#endif
#ifdef CHIAKI_GUI_ENABLE_SPEEX
    void speechProcessingChanged();
    void noiseSuppressLevelChanged();
    void echoSuppressLevelChanged();
#endif
    void fullscreenDoubleClickChanged();
    void remotePlayAskChanged();
    void addSteamShortcutAskChanged();
    void hideCursorChanged();
    void hapticOverrideChanged();
    void audioVideoDisabledChanged();
    void showStreamStatsChanged();
    void streamerModeChanged();
    void fpsLocalPS4Changed();
    void fpsRemotePS4Changed();
    void fpsLocalPS5Changed();
    void fpsRemotePS5Changed();
    void bitrateLocalPS4Changed();
    void bitrateRemotePS4Changed();
    void bitrateLocalPS5Changed();
    void bitrateRemotePS5Changed();
    void codecLocalPS5Changed();
    void codecRemotePS5Changed();
    void vulkanDeferredSwapChanged();
    void displayTargetPeakChanged();
    void displayTargetContrastChanged();
    void displayTargetPrimChanged();
    void displayTargetTrcChanged();
    void audioBufferSizeChanged();
    void audioVolumeChanged();
    void audioOutDeviceChanged();
    void audioInDeviceChanged();
    void wifiDroppedNotifChanged();
    void portGuessingEnabledChanged();
    void portGuessCountChanged();
    void portGuessSocketCountChanged();
    void decoderChanged();
    void useZeroCopyChanged();
    void windowTypeChanged();
    void customResolutionWidthChanged();
    void customResolutionHeightChanged();
    void sZoomFactorChanged();
    void videoPresetChanged();
    void rendererBackendChanged();
    void autoConnectMacChanged();
    void audioDevicesChanged();
    void registeredHostsChanged();
    void psnAuthTokenChanged();
    void psnRefreshTokenChanged();
    void psnAuthTokenExpiryChanged();
    void psnAccountIdChanged();
    void mouseTouchEnabledChanged();
    void keyboardEnabledChanged();
    void dpadTouchEnabledChanged();
    void dpadTouchIncrementChanged();
    void dpadTouchShortcut1Changed();
    void dpadTouchShortcut2Changed();
    void dpadTouchShortcut3Changed();
    void dpadTouchShortcut4Changed();
    void streamMenuEnabledChanged();
    void streamMenuShortcut1Changed();
    void streamMenuShortcut2Changed();
    void streamMenuShortcut3Changed();
    void streamMenuShortcut4Changed();
    void controllerMappingChanged();
    void packetLossReportedMaxChanged();
    void iDROnFECFailureEnabledChanged();
    void currentProfileChanged();
    void profilesChanged();
    void placeboUpscalerChanged();
    void placeboPlaneUpscalerChanged();
    void placeboDownscalerChanged();
    void placeboPlaneDownscalerChanged();
    void placeboFrameMixerChanged();
    void directFrameMappingChanged();
    void placeboDeinterlaceEnabledChanged();
    void placeboDeinterlacePresetChanged();
    void placeboDeinterlaceAlgorithmChanged();
    void placeboDeinterlaceSkipSpatialChanged();
    void placeboAntiringingStrengthChanged();
    void placeboDebandEnabledChanged();
    void placeboDebandPresetChanged();
    void placeboDebandIterationsChanged();
    void placeboDebandThresholdChanged();
    void placeboDebandRadiusChanged();
    void placeboDebandGrainChanged();
    void placeboSigmoidEnabledChanged();
    void placeboSigmoidPresetChanged();
    void placeboSigmoidCenterChanged();
    void placeboSigmoidSlopeChanged();
    void placeboColorAdjustmentEnabledChanged();
    void placeboColorAdjustmentPresetChanged();
    void placeboColorAdjustmentBrightnessChanged();
    void placeboColorAdjustmentContrastChanged();
    void placeboColorAdjustmentSaturationChanged();
    void placeboColorAdjustmentHueChanged();
    void placeboColorAdjustmentGammaChanged();
    void placeboColorAdjustmentTemperatureChanged();
    void placeboPeakDetectionEnabledChanged();
    void placeboPeakDetectionPresetChanged();
    void placeboPeakDetectionPeakSmoothingPeriodChanged();
    void placeboPeakDetectionSceneThresholdLowChanged();
    void placeboPeakDetectionSceneThresholdHighChanged();
    void placeboPeakDetectionPeakPercentileChanged();
    void placeboPeakDetectionBlackCutoffChanged();
    void placeboPeakDetectionAllowDelayedPeakChanged();
    void placeboColorMappingEnabledChanged();
    void placeboColorMappingPresetChanged();
    void placeboGamutMappingFunctionChanged();
    void placeboGamutMappingPerceptualDeadzoneChanged();
    void placeboGamutMappingPerceptualStrengthChanged();
    void placeboGamutMappingColorimetricGammaChanged();
    void placeboGamutMappingSoftClipKneeChanged();
    void placeboGamutMappingSoftClipDesatChanged();
    void placeboGamutMappingLut3dSizeIChanged();
    void placeboGamutMappingLut3dSizeCChanged();
    void placeboGamutMappingLut3dSizeHChanged();
    void placeboGamutMappingLut3dTricubicEnabledChanged();
    void placeboGamutMappingGamutExpansionEnabledChanged();
    void placeboToneMappingFunctionChanged();
    void placeboToneMappingKneeAdaptationChanged();
    void placeboToneMappingKneeMinimumChanged();
    void placeboToneMappingKneeMaximumChanged();
    void placeboToneMappingKneeDefaultChanged();
    void placeboToneMappingKneeOffsetChanged();
    void placeboToneMappingSlopeTuningChanged();
    void placeboToneMappingSlopeOffsetChanged();
    void placeboToneMappingSplineContrastChanged();
    void placeboToneMappingReinhardContrastChanged();
    void placeboToneMappingLinearKneeChanged();
    void placeboToneMappingExposureChanged();
    void placeboToneMappingInverseToneMappingEnabledChanged();
    void placeboToneMappingMetadataChanged();
    void placeboToneMappingToneLutSizeChanged();
    void placeboToneMappingContrastRecoveryChanged();
    void placeboToneMappingContrastSmoothnessChanged();
    void placeboChanged();

private:
    Settings *settings = {};
    QStringList audio_in_devices;
    QStringList audio_out_devices;
};
