#pragma once

#include "streamsession.h"
#include "discoverymanager.h"
#include "qmlmainwindow.h"
#include "qmlcontroller.h"
#include "qmlsettings.h"

#include <QObject>
#include <QMutex>
#include <QThread>
#include <QJSValue>
#include <QSet>
#include <QUrl>
#include <QFutureWatcher>
#include <QFuture>
#include <QAtomicInteger>
#ifdef CHIAKI_HAVE_WEBENGINE
#include <QQuickWebEngineProfile>
#include <QWebEngineUrlRequestInterceptor>
#endif

class SystemdInhibit;
#ifdef Q_OS_MACOS
    class MacWakeSleep;
#elif defined(Q_OS_WINDOWS)
    class WindowsWakeSleep;
#endif

class QmlRegist : public QObject
{
    Q_OBJECT

public:
    QmlRegist(const ChiakiRegistInfo &regist_info, uint32_t log_mask, QObject *parent = nullptr);

signals:
    void log(ChiakiLogLevel level, const QString &msg);
    void failed();
    void success(const RegisteredHost& host);

private:
    static void log_cb(ChiakiLogLevel level, const char *msg, void *user);
    static void regist_cb(ChiakiRegistEvent *event, void *user);

    ChiakiLog chiaki_log;
    ChiakiRegist chiaki_regist;
};

class PsnConnectionWorker : public QObject
{
    Q_OBJECT

public:
    void ConnectPsnConnection(StreamSession *session, const QString &duid, const bool &ps5);

signals:
    void resultReady(const ChiakiErrorCode &err);
};

#ifdef CHIAKI_HAVE_WEBENGINE
class SecUaRequestInterceptor : public QWebEngineUrlRequestInterceptor {
    Q_OBJECT

public:
    SecUaRequestInterceptor(QString version, QObject *p = Q_NULLPTR) : QWebEngineUrlRequestInterceptor(p) { chrome_version = version; };
    void interceptRequest(QWebEngineUrlRequestInfo &info) override {
#ifdef Q_OS_WINDOWS
        info.setHttpHeader("Sec-Ch-Ua", QString("\"Not(A:Brand\";v=\"99\", \"Microsoft Edge\";v=\"%1\", \"Chromium\";v=\"%1\"").arg(chrome_version).toUtf8());
#else
        info.setHttpHeader("Sec-Ch-Ua", QString("\"Not(A:Brand\";v=\"99\", \"Google Chrome\";v=\"%1\", \"Chromium\";v=\"%1\"").arg(chrome_version).toUtf8());
#endif
    }
private:
    QString chrome_version;
};
#endif
class QmlBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QmlMainWindow* window READ qmlWindow CONSTANT)
    Q_PROPERTY(QmlSettings* settings READ qmlSettings CONSTANT)
    Q_PROPERTY(StreamSession* session READ qmlSession NOTIFY sessionChanged)
    Q_PROPERTY(QList<QmlController*> controllers READ qmlControllers NOTIFY controllersChanged)
    Q_PROPERTY(bool discoveryEnabled READ discoveryEnabled WRITE setDiscoveryEnabled NOTIFY discoveryEnabledChanged)
    Q_PROPERTY(QVariantList hosts READ hosts NOTIFY hostsChanged)
    Q_PROPERTY(QVariantList hiddenHosts READ hiddenHosts NOTIFY hiddenHostsChanged)
    Q_PROPERTY(bool autoConnect READ autoConnect NOTIFY autoConnectChanged)
    Q_PROPERTY(PsnConnectState connectState READ connectState WRITE setConnectState NOTIFY connectStateChanged)
    Q_PROPERTY(QVariantList currentControllerMapping READ currentControllerMapping NOTIFY currentControllerMappingChanged)
    Q_PROPERTY(QString currentControllerType READ currentControllerType NOTIFY currentControllerTypeChanged)
    Q_PROPERTY(bool controllerMappingDefaultMapping READ controllerMappingDefaultMapping NOTIFY controllerMappingDefaultMappingChanged)
    Q_PROPERTY(bool controllerMappingInProgress READ controllerMappingInProgress NOTIFY controllerMappingInProgressChanged)
    Q_PROPERTY(bool controllerMappingAltered READ controllerMappingAltered NOTIFY controllerMappingAlteredChanged)
    Q_PROPERTY(bool enableAnalogStickMapping READ enableAnalogStickMapping WRITE setEnableAnalogStickMapping NOTIFY enableAnalogStickMappingChanged)

public:

    enum class PsnConnectState
    {
        NotStarted,
        WaitingForInternet,
        InitiatingConnection,
        LinkingConsole,
        RegisteringConsole,
        RegistrationFinished,
        DataConnectionStart,
        DataConnectionFinished,
        ConnectFailed,
        ConnectFailedStart,
        ConnectFailedConsoleUnreachable,
    };
    Q_ENUM(PsnConnectState);
    QmlBackend(Settings *settings, QmlMainWindow *window);
    ~QmlBackend();

    QmlMainWindow *qmlWindow() const;
    QmlSettings *qmlSettings() const;
    StreamSession *qmlSession() const;
    QList<QmlController*> qmlControllers() const;

    bool discoveryEnabled() const;
    void setDiscoveryEnabled(bool enabled);
    void refreshAuth();

    PsnConnectState connectState() const;
    void setConnectState(PsnConnectState connect_state);
    QVariantList hosts() const;

    QVariantList hiddenHosts() const;

    QVariantList currentControllerMapping() const;

    QString currentControllerType() const { return controller_mapping_controller_type; }

    void controllerMappingChangeButton(QString button);

    void controllerMappingUpdate(Controller *controller);

    bool controllerMappingDefaultMapping() const { return controller_mapping_default_mapping; }
    void setControllerMappingDefaultMapping(bool is_default_mapping);

    bool controllerMappingAltered() const { return controller_mapping_altered; }
    void setControllerMappingAltered(bool altered);

    bool controllerMappingInProgress() const  {return controller_mapping_in_progress; }
    void setControllerMappingInProgress(bool is_in_progress);

    bool enableAnalogStickMapping() const { return enable_analog_stick_mapping; }
    void setEnableAnalogStickMapping(bool enabled);

    void finishAutoRegister(const ChiakiRegisteredHost &host);

    bool autoConnect() const;

    void psnConnector();

    void createSession(const StreamSessionConnectInfo &connect_info);

    void psnSessionStart();
    void startSession(bool emit_session_changed = true);

    void checkPsnConnection(const ChiakiErrorCode &err);

    void checkNickname(QString nickname);

    bool closeRequested();

    void setAllowJoystickBackgroundEvents();

    void setIsAppActive();

    void profileChanged();
    bool prepareFrameForPresentation(ChiakiFfmpegFrame &frame, bool use_opengl_renderer);
    void prepareStartupPresentation(bool present_warmup);
    void applyStartupWindowSizing();

    void goToSleep();

    bool zeroCopy()        { return !disable_zero_copy; };
    void disableZeroCopy() { disable_zero_copy = true; };
    void enableZeroCopy()  { disable_zero_copy = false; };

    Q_INVOKABLE void deleteHost(int index);
    Q_INVOKABLE void wakeUpHost(int index, QString nickname = QString());
    Q_INVOKABLE void addManualHost(int index, const QString &address);
    Q_INVOKABLE void hideHost(const QString &mac_string, const QString &host_nickname);
    Q_INVOKABLE void unhideHost(const QString &mac_string);
    Q_INVOKABLE bool registerHost(const QString &host, const QString &psn_id, const QString &pin, const QString &cpin, bool broadcast, int target, const QJSValue &callback);
    Q_INVOKABLE void connectToHost(int index, QString nickname = QString());
    Q_INVOKABLE void stopSession(bool sleep);
    Q_INVOKABLE void sessionGoHome();
    Q_INVOKABLE void enterPin(const QString &pin);
    Q_INVOKABLE QUrl psnLoginUrl() const;
    Q_INVOKABLE bool checkPsnRedirectURL(const QUrl &url) const;
    Q_INVOKABLE bool handlePsnLoginRedirect(const QUrl &url);
    Q_INVOKABLE void stopAutoConnect();
    Q_INVOKABLE void setConsolePin(int index, QString console_pin);
    Q_INVOKABLE QString openPsnLink();
    Q_INVOKABLE QString openPlaceboOptionsLink();
    Q_INVOKABLE void initPsnAuth(const QUrl &url, const QJSValue &callback);
    Q_INVOKABLE void psnCancel(bool stop_thread);
    Q_INVOKABLE void refreshPsnToken();
    Q_INVOKABLE void beginControllerMapping(bool reset_mapping);
    Q_INVOKABLE void creatingControllerMapping(bool creating_controller_mapping);
    Q_INVOKABLE void updateButton(int chiaki_button, QString physical_button, int new_index);
    Q_INVOKABLE void controllerMappingSelectButton();
    Q_INVOKABLE void controllerMappingReset();
    Q_INVOKABLE void controllerMappingQuit();
    Q_INVOKABLE void controllerMappingButtonQuit();
    Q_INVOKABLE void controllerMappingApply();
    Q_INVOKABLE void autoRegister();
#if CHIAKI_GUI_ENABLE_STEAM_SHORTCUT
    Q_INVOKABLE void createSteamShortcut(QString shortcutName, QString launchOptions, const QJSValue &callback, QString steamDir);
#endif
#ifdef CHIAKI_HAVE_WEBENGINE
    Q_INVOKABLE void setWebEngineHints(QQuickWebEngineProfile *profile);
    Q_INVOKABLE void clearCookies(QQuickWebEngineProfile *profile);
#endif

signals:
    void sessionChanged(StreamSession *session);
    void psnConnect(StreamSession *session, const QString &duid, const bool &ps5);
    void showPsnView();
    void connectStateChanged();
    void controllersChanged();
    void currentControllerMappingChanged();
    void currentControllerTypeChanged();
    void controllerMappingInProgressChanged();
    void controllerMappingDefaultMappingChanged();
    void controllerMappingButtonSelected(QString button);
    void controllerMappingAlteredChanged();
    void controllerMappingSteamControllerSelected();
    void enableAnalogStickMappingChanged();
    void discoveryEnabledChanged();
    void hostsChanged();
    void hiddenHostsChanged();
    void psnTokenChanged();
    void psnCredsExpired();
    void autoConnectChanged();
    void wakeupStartInitiated();
    void wakeupStartFailed();
    void windowTypeUpdated(WindowType type);

    void error(const QString &title, const QString &text);
    void sessionError(const QString &title, const QString &text);
    void sessionPinDialogRequested();
    void sessionStopDialogRequested();
    void registDialogRequested(const QString &host, bool ps5, const QString &duid);
    void psnLoginAccountIdDone(const QString &accountId);
    void psnLoginAccountIdError(const QString &error);

private:
    struct DisplayServer {
        bool valid = false;

        DiscoveryHost discovery_host;
        ManualHost manual_host;
        bool discovered;

        PsnHost psn_host;
        QString duid;
        RegisteredHost registered_host;
        bool registered;

        QString GetHostAddr() const { return discovered ? discovery_host.host_addr : manual_host.GetHost(); }
        bool IsPS5() const { return discovered ? discovery_host.ps5 :
            (registered ? chiaki_target_is_ps5(registered_host.GetTarget()) : true); }
    };

    DisplayServer displayServerAt(int index) const;
    bool sendWakeup(const DisplayServer &server);
    bool sendWakeup(const QString &host, const QByteArray &regist_key, bool ps5);
    void updateControllers();
    void updateControllerMappings();
    void updateDiscoveryHosts();
    void updatePsnHosts();
    void updatePsnHostsThread();
    void updateAudioVolume();
    void resumeFromSleep();
    uint32_t getStreamShortcut() const;
    void updateStreamShortcut();
    QString getExecutable();
    void teardownSession();

    Settings *settings = {};
    QmlSettings *settings_qml = {};
    QmlMainWindow *window = {};
    StreamSession *session = {};
    QThread *frame_thread = {};
    QTimer *psn_reconnect_timer = {};
    QTimer *psn_auto_connect_timer = {};
    QTimer *wakeup_start_timer = {};
    int psn_reconnect_tries = 0;
    QThread psn_connection_thread;
    PsnConnectState psn_connect_state;
    DiscoveryManager discovery_manager;
    QList<QString> waking_sleeping_nicknames;
    QHash<int, QmlController*> controllers;
    DisplayServer regist_dialog_server;
    StreamSessionConnectInfo session_info = {};
    SystemdInhibit *sleep_inhibit = {};
#ifdef Q_OS_MACOS
    MacWakeSleep *mac_wake_sleep = {};
#elif defined(Q_OS_WINDOWS)
    WindowsWakeSleep *windows_wake_sleep = {};
#endif
    bool controller_mapping_default_mapping = false;
    bool controller_mapping_altered = false;
    bool updating_psn_hosts = false;
    QFutureWatcher<void> psn_hosts_watcher;
    QFuture<void> psn_hosts_future;
    bool disable_zero_copy = false;
    QMutex hw_transfer_state_mutex;
    QSet<int> logged_hw_transfer_formats;
    QSet<int> logged_hw_transfer_failures;
    QAtomicInteger<int> pending_recovered_frame = 0;
    Controller *controller_mapping_controller = {};
    QMap<QString, QStringList> controller_guids_to_update = {};
    int controller_mapping_id = -1;
    QString controller_mapping_controller_guid = "";
    QString controller_mapping_controller_vid_pid = "";
    QString controller_mapping_controller_type = "";
    QMap<QString, QStringList> controller_mapping_controller_mappings = {};
    QMap<QString, QStringList> controller_mapping_applied_controller_mappings = {};
    QMap<QString, QString> controller_mapping_physical_button_mappings = {};
    QMap<QString, QString> controller_mapping_original_controller_mappings = {};
    bool controller_mapping_reset_requested = false;
    bool controller_mapping_in_progress = false;
    bool enable_analog_stick_mapping = false;
    bool resume_session = false;
    bool settings_allocd = false;
    HostMAC auto_connect_mac = {};
    QString auto_connect_nickname = "";
    QString wakeup_nickname = "";
    bool wakeup_start = false;
    QMap<QString, PsnHost> psn_hosts = {};
    QMap<QString, PsnHost> psn_nickname_hosts = {};
#ifdef CHIAKI_HAVE_WEBENGINE
    SecUaRequestInterceptor * request_interceptor = {};
#endif
};
