// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2011 Craig Drummond <craig.p.drummond@gmail.com>
// SPDX-FileCopyrightText: 2018 Alexis Lopes Zubeta <contact@azubieta.net>
// SPDX-FileCopyrightText: 2020 Tomaz Canabrava <tcanabrava@kde.org>
/*
 * UFW KControl Module
 */

#include "ufwclient.h"
#include "ufwlogmodel.h"

#include "rule.h"
#include "types.h"

#include <QDebug>
#include <QDir>
#include <QNetworkInterface>
#include <QStandardPaths>
#include <QTimer>
#include <QVariantMap>
#include <QXmlStreamReader>
#include <QProcess>

#include <KConfig>
#include <KConfigGroup>
#include <KLocalizedString>
#include <KPluginFactory>

#include <loglistmodel.h>
#include <rulelistmodel.h>

K_PLUGIN_CLASS_WITH_JSON(UfwClient, "ufwbackend.json")

namespace
{
void debugState(KAuth::Action::AuthStatus status)
{
    using Status = KAuth::Action::AuthStatus;
    switch (status) {
    case Status::AuthorizedStatus:
        qDebug() << "Job Authorized";
        break;
    case Status::AuthRequiredStatus:
        qDebug() << "Job Requires authentication";
        break;
    case Status::UserCancelledStatus:
        qDebug() << "User cancelled!";
        break;
    case Status::DeniedStatus:
        qDebug() << "Password denied";
        break;
    case Status::InvalidStatus:
        qDebug() << "Invalid Status!";
        break;
    case Status::ErrorStatus:
        qDebug() << "Job is in an error state";
        break;
    }
}
}

UfwClient::UfwClient(QObject *parent, const QVariantList &args)
    : IFirewallClientBackend(parent, args)
    , m_rulesModel(new RuleListModel(this))
    , m_logs(nullptr)
    , m_busy(false)

{
    queryExecutable("ufw");
}

QString UfwClient::name() const
{
    return QStringLiteral("ufw");
}

bool UfwClient::isTcpAndUdp(int protocolIdx)
{
    return protocolIdx == 0;
}

void UfwClient::refresh()
{
    queryStatus(FirewallClient::DefaultDataBehavior::ReadDefaults, FirewallClient::ProfilesBehavior::ListenProfiles);
}

bool UfwClient::enabled() const
{
    return m_currentProfile.enabled();
}

KJob *UfwClient::setEnabled(bool value)
{
    if (enabled() == value) {
        return nullptr;
    }

    QVariantMap args {
        {"cmd", "setStatus"},
        {"status", value},
    };

    KAuth::Action modifyAction = buildModifyAction(args);
    qDebug() << "Starting the set Enabled job";
    KAuth::ExecuteJob *job = modifyAction.execute();

    connect(job, &KAuth::ExecuteJob::result, this, [this, job] {
        qDebug() << "Set Enabled job finished, triggering a status query.";
        if (job->error() == KJob::NoError) {
            queryStatus(FirewallClient::DefaultDataBehavior::ReadDefaults, FirewallClient::ProfilesBehavior::DontListenProfiles);
        } else {
            qDebug() << job->error();
        }
    });

    return job;
}

KJob *UfwClient::queryStatus(FirewallClient::DefaultDataBehavior defaultsBehavior, FirewallClient::ProfilesBehavior profilesBehavior)
{
    qDebug() << "Status query starting";
    if (m_busy) {
        qWarning() << "Ufw client is busy";
        return nullptr;
    }

    m_busy = true;

    const bool readDefaults = defaultsBehavior == FirewallClient::DefaultDataBehavior::ReadDefaults;
    const bool listProfiles = profilesBehavior == FirewallClient::ProfilesBehavior::ListenProfiles;

    QVariantMap args {
        {"defaults", readDefaults},
        {"profiles", listProfiles},
    };

    if (m_queryAction.name().isEmpty()) {
        m_queryAction = buildQueryAction(args);
    }

    KAuth::ExecuteJob *job = m_queryAction.execute();
    connect(job, &KAuth::ExecuteJob::result, this, [this, job] {
        qDebug() << "Status Query finished, setting the profile";
        m_busy = false;

        if (job->error() != KJob::NoError) {
            emit showErrorMessage(i18n("There was an error in the backend! Please report it.\n%1 %2", job->action().name(), job->errorString()));
            qWarning() << job->action().name() << job->errorString();
            return;
        }
        QByteArray response = job->data().value("response", "").toByteArray();
        setProfile(Profile(response));
    });

    qDebug() << "Starting the Status Query";
    job->start();
    return job;
}

KJob *UfwClient::setDefaultIncomingPolicy(QString policy)
{
    if (policy == defaultIncomingPolicy()) {
        return nullptr;
    }

    const QString xmlArg = QStringLiteral("<defaults incoming=\"%1\"/>").arg(policy);

    QVariantMap args {
        {"cmd", "setDefaults"},
        {"xml", xmlArg},
    };

    KAuth::Action modifyAction = buildModifyAction(args);

    KAuth::ExecuteJob *job = modifyAction.execute();
    connect(job, &KAuth::ExecuteJob::result, this, [this, job] {
        if (!job->error()) {
            queryStatus(FirewallClient::DefaultDataBehavior::ReadDefaults, FirewallClient::ProfilesBehavior::DontListenProfiles);
        }
    });

    job->start();
    return job;
}

KJob *UfwClient::setDefaultOutgoingPolicy(QString policy)
{
    if (policy == defaultOutgoingPolicy()) {
        return nullptr;
    }

    const QString xmlArg = QStringLiteral("<defaults outgoing=\"%1\"/>").arg(policy);

    QVariantMap args {
        {"cmd", "setDefaults"},
        {"xml", xmlArg},
    };

    KAuth::Action modifyAction = buildModifyAction(args);

    KAuth::ExecuteJob *job = modifyAction.execute();
    connect(job, &KAuth::ExecuteJob::result, this, [this, job] {
        if (!job->error()) {
            queryStatus(FirewallClient::DefaultDataBehavior::ReadDefaults, FirewallClient::ProfilesBehavior::DontListenProfiles);
        }
    });

    job->start();
    return job;
}

void UfwClient::setLogsAutoRefresh(bool logsAutoRefresh)
{
    if (m_logsAutoRefresh == logsAutoRefresh)
        return;

    if (logsAutoRefresh) {
        connect(&m_logsRefreshTimer, &QTimer::timeout, this, &UfwClient::refreshLogs);
        m_logsRefreshTimer.setInterval(3000);
        m_logsRefreshTimer.start();
    } else {
        disconnect(&m_logsRefreshTimer, &QTimer::timeout, this, &UfwClient::refreshLogs);
        m_logsRefreshTimer.stop();
    }

    m_logsAutoRefresh = logsAutoRefresh;
    emit logsAutoRefreshChanged(m_logsAutoRefresh);
}

void UfwClient::refreshLogs()
{
    if (!m_logs) {
        logs();
        qWarning() << "Trying to refresh logs without logs model, creating the object.";
        return;
    }

    KAuth::Action action("org.kde.ufw.viewlog");
    action.setHelperId("org.kde.ufw");

    QVariantMap args;
    if (m_rawLogs.size() > 0)
        args["lastLine"] = m_rawLogs.last();

    action.setArguments(args);

    m_logs->setBusy(true);

    KAuth::ExecuteJob *job = action.execute();
    connect(job, &KAuth::ExecuteJob::finished, this, [this, job] {
        m_logs->setBusy(false);

        if (job->error()) {
            emit m_logs->showErrorMessage(i18n("Error fetching firewall logs: %1", job->errorString()));
            return;
        }

        const QStringList newLogs = job->data().value("lines", "").toStringList();
        // FIXME do we really need to store this raw thing here and then processed in the model?
        m_rawLogs.append(newLogs);
        m_logs->addRawLogs(newLogs);
    });

    job->start();
}

void UfwClient::setProfile(Profile profile)
{
    qDebug() << "Profile Received, Setting the profile on the model";
    auto oldProfile = m_currentProfile;
    m_currentProfile = profile;
    m_rulesModel->setProfile(m_currentProfile);
    if (m_currentProfile.enabled() != oldProfile.enabled())
        emit enabledChanged(m_currentProfile.enabled());

    if (m_currentProfile.defaultIncomingPolicy() != oldProfile.defaultIncomingPolicy()) {
        const QString policy = Types::toString(m_currentProfile.defaultIncomingPolicy());
        emit defaultIncomingPolicyChanged(policy);
    }

    if (m_currentProfile.defaultOutgoingPolicy() != oldProfile.defaultOutgoingPolicy()) {
        const QString policy = Types::toString(m_currentProfile.defaultOutgoingPolicy());
        emit defaultOutgoingPolicyChanged(policy);
    }
}

KAuth::Action UfwClient::buildQueryAction(const QVariantMap &arguments)
{
    auto action = KAuth::Action("org.kde.ufw.query");
    action.setHelperId("org.kde.ufw");
    action.setArguments(arguments);

    return action;
}

KAuth::Action UfwClient::buildModifyAction(const QVariantMap &arguments)
{
    auto action = KAuth::Action("org.kde.ufw.modify");
    action.setHelperId("org.kde.ufw");
    action.setArguments(arguments);

    return action;
}

RuleListModel *UfwClient::rules() const
{
    return m_rulesModel;
}

Rule *UfwClient::ruleAt(int index)
{
    auto rules = m_currentProfile.rules();

    if (index < 0 || index >= rules.count()) {
        return nullptr;
    }

    Rule *rule = rules.at(index);
    return rule;
}

KJob *UfwClient::addRule(Rule *r)
{
    if (!r) {
        qWarning() << "nullptr rule";
        return nullptr;
    }


    QVariantMap args {
        {"cmd", "addRules"},
        {"count", 1},
        {"xml0", toXml(r)},
    };

    KAuth::Action modifyAction = buildModifyAction(args);

    KAuth::ExecuteJob *job = modifyAction.execute();
    connect(job, &KAuth::ExecuteJob::result, this, [this, job] {
        if (job->error() == KJob::NoError) {
            queryStatus(FirewallClient::DefaultDataBehavior::ReadDefaults, FirewallClient::ProfilesBehavior::ListenProfiles);
        }
    });

    job->start();
    return job;
}

KJob *UfwClient::removeRule(int index)
{
    if (index < 0 || index >= m_currentProfile.rules().count()) {
        qWarning() << __FUNCTION__ << "invalid rule index";
        return nullptr;
    }

    // Correct index
    index += 1;

    QVariantMap args {
        {"cmd", "removeRule"},
        {"index", QString::number(index)},
    };

    KAuth::Action modifyAction = buildModifyAction(args);
    KAuth::ExecuteJob *job = modifyAction.execute();
    connect(job, &KAuth::ExecuteJob::statusChanged, this, [this](KAuth::Action::AuthStatus status) { debugState(status); });

    connect(job, &KAuth::ExecuteJob::result, this, [this, job] {
        if (!job->error()) {
            queryStatus(FirewallClient::DefaultDataBehavior::ReadDefaults, FirewallClient::ProfilesBehavior::ListenProfiles);
        }
    });

    job->start();
    return job;
}

KJob *UfwClient::updateRule(Rule *r)
{
    /* UFW does not support update rules. the tests showed that remove / insert is flacky. */
    Q_UNUSED(r);
    return nullptr;
}

KJob *UfwClient::moveRule(int from, int to)
{
    const QVector<Rule*> rules = m_currentProfile.rules();
    if (from < 0 || from >= rules.count()) {
        qWarning() << "invalid from index";
        return nullptr;
    }

    if (to < 0 || to >= rules.count()) {
        qWarning() << "invalid to index";
        return nullptr;
    }

    // Correct indices
    from += 1;
    to += 1;

    QVariantMap args {
        {"cmd", "moveRule"},
        {"from", from},
        {"to", to},
    };

    KAuth::Action modifyAction = buildModifyAction(args);
    KAuth::ExecuteJob *job = modifyAction.execute();
    connect(job, &KAuth::ExecuteJob::finished, this, [this, job] {
        if (!job->error()) {
            queryStatus(FirewallClient::DefaultDataBehavior::ReadDefaults, FirewallClient::ProfilesBehavior::ListenProfiles);
        }
    });

    job->start();
    return job;
}

QString UfwClient::defaultIncomingPolicy() const
{
    auto policy_t = m_currentProfile.defaultIncomingPolicy();
    return Types::toString(policy_t);
}

QString UfwClient::defaultOutgoingPolicy() const
{
    auto policy_t = m_currentProfile.defaultOutgoingPolicy();
    return Types::toString(policy_t);
}

LogListModel *UfwClient::logs()
{
    if (!m_logs) {
        m_logs = new UfwLogModel(this);
        refreshLogs();
    }
    return m_logs;
}

bool UfwClient::logsAutoRefresh() const
{
    return m_logsAutoRefresh;
}

namespace {
    bool isNumber(const QString& s) {
        bool error = true;
        s.toInt(&error);
        return error;
    }

    QString portStrToInt(const QString& portStr) {
        QFile file("/etc/services");
        if (!file.open(QIODevice::ReadOnly)) {
            qDebug() << "Could not open file, returning";
            return portStr;
        }
        while (!file.atEnd()) {
            QString line = file.readLine();
            if (!line.startsWith(portStr.toLocal8Bit())) {
                continue;
            }

            // http      80/tcp
            auto list = line.split(QRegExp("\\s+"));
            if (list.size() > 1) {
                if (list[1].contains('/')) {
                    return list[1].split('/')[0];
                } else {
                    return list[1];
                }
            }
        }
        return "";
    }
}

Rule *UfwClient::createRuleFromConnection(const QString &protocol, const QString &localAddress, const QString &foreignAddres, const QString &status)
{
    // FIXME use a regexp for that and support ipv6!
    auto _localAddress = localAddress;
    _localAddress.replace("*", "");
    _localAddress.replace("0.0.0.0", "");

    auto _foreignAddres = foreignAddres;
    _foreignAddres.replace("*", "");
    _foreignAddres.replace("0.0.0.0", "");

    auto localAddressData = _localAddress.split(":");
    auto foreignAddresData = _foreignAddres.split(":");

    if (!isNumber(localAddressData[1])) {
        localAddressData[1] = portStrToInt(localAddressData[1]);
    }
    if (!isNumber(foreignAddresData[1])) {
        foreignAddresData[1] = portStrToInt(foreignAddresData[1]);
    }

    auto rule = new Rule();
    rule->setIncoming(status == QStringLiteral("LISTEN"));
    rule->setPolicy("deny");

    qDebug() << "-----------------------";
    qDebug() << foreignAddresData << localAddressData;
    qDebug() << "------------------------";

    // Prepare rule draft
    if (status == QStringLiteral("LISTEN")) {
        rule->setSourceAddress(foreignAddresData[0]);
        rule->setSourcePort(foreignAddresData[1]);
        rule->setDestinationAddress(localAddressData[0]);
        rule->setDestinationPort(localAddressData[1]);
    } else {
        rule->setSourceAddress(localAddressData[0]);
        rule->setSourcePort(localAddressData[1]);
        rule->setDestinationAddress(foreignAddresData[0]);
        rule->setDestinationPort(foreignAddresData[1]);
    }

    rule->setProtocol(knownProtocols().indexOf(protocol.toUpper()));
    return rule;
}

Rule *UfwClient::createRuleFromLog(const QString &protocol, const QString &sourceAddress, const QString &sourcePort, const QString &destinationAddress, const QString &destinationPort, const QString &inn)
{
    // Transform to the ufw notation
    auto rule = new Rule();

    auto _sourceAddress = sourceAddress;
    _sourceAddress.replace("*", "");
    _sourceAddress.replace("0.0.0.0", "");

    auto _destinationAddress = destinationAddress;
    _destinationAddress.replace("*", "");
    _destinationAddress.replace("0.0.0.0", "");

    // Heuristic to determine whether we should be ipv6
    // TODO error when one is ipv6 and the other isn't?
    if (sourceAddress.contains(QLatin1Char(':')) && destinationAddress.contains(QLatin1Char(':'))) {
        rule->setIpv6(true);
    }

    // Prepare rule draft
    rule->setIncoming(inn.size());
    rule->setPolicy("deny");
    rule->setSourceAddress(_sourceAddress);
    rule->setSourcePort(sourcePort);

    rule->setDestinationAddress(_destinationAddress);
    rule->setDestinationPort(destinationPort);

    rule->setProtocol(knownProtocols().indexOf(protocol.toUpper()));
    return rule;
}

IFirewallClientBackend *UfwClient::createMethod(FirewallClient *parent)
{
    IFirewallClientBackend *instance = new UfwClient(parent, {} /*args*/);
    return instance;
}

void UfwClient::refreshProfiles()
{
    static const char *constProfileDir = "/etc/ufw/applications.d/";

    const QStringList files(QDir(constProfileDir).entryList(QDir::NoDotAndDotDot));

    QVector<Entry> profiles;
    for (const auto &file : files) {
        KConfig cfg(constProfileDir + file, KConfig::SimpleConfig);

        for (const auto &group : cfg.groupList()) {
            const QString ports(cfg.group(group).readEntry("ports", QString()));

            if (!ports.isEmpty() && !profiles.contains(group)) {
                profiles.append(Entry(group, ports));
            }
        }
    }

    setProfiles(profiles);
}

QStringList UfwClient::knownProtocols() {
    return {i18n("Any"), "TCP", "UDP"};
}

QString UfwClient::toXml(Rule *r) const
{
    QString xmlString;

    QXmlStreamWriter xml(&xmlString);

    xml.writeStartElement(QStringLiteral("rule"));

    if (r->position() != 0) {
        qDebug() << "Getting the position" << r->position();
        xml.writeAttribute(QStringLiteral("position"), QString::number(r->position()));
    }

    xml.writeAttribute(QStringLiteral("action"), Types::toString(r->action()));
    xml.writeAttribute(QStringLiteral("direction"), r->incoming() ? QStringLiteral("in") : QStringLiteral("out"));

    if (!r->destinationApplication().isEmpty()) {
        xml.writeAttribute(QStringLiteral("dapp"), r->destinationApplication());
    } else if (!r->destinationPort().isEmpty()) {
        xml.writeAttribute(QStringLiteral("dport"), r->destinationPort());
    }
    if (!r->sourceApplication().isEmpty()) {
        xml.writeAttribute(QStringLiteral("sapp"), r->sourceApplication());
    } else if (!r->sourcePort().isEmpty()) {
        xml.writeAttribute(QStringLiteral("sport"), r->sourcePort());
    }

    if (!FirewallClient::isTcpAndUdp(r->protocol())) {
        xml.writeAttribute(QStringLiteral("protocol"), FirewallClient::knownProtocols().at(r->protocol()));
    }

    if (!r->destinationAddress().isEmpty()) {
        xml.writeAttribute(QStringLiteral("dst"), r->destinationAddress());
    }
    if (!r->sourceAddress().isEmpty()) {
        xml.writeAttribute(QStringLiteral("src"), r->sourceAddress());
    }

    if (!r->interfaceIn().isEmpty()) {
        xml.writeAttribute(QStringLiteral("interface_in"), r->interfaceIn());
    }
    if (!r->interfaceOut().isEmpty()) {
        xml.writeAttribute(QStringLiteral("interface_out"), r->interfaceOut());
    }

    xml.writeAttribute(QStringLiteral("logtype"), Types::toString(r->logging()));


    xml.writeAttribute(QStringLiteral("v6"), r->ipv6() ? QStringLiteral("True") : QStringLiteral("False"));

    xml.writeEndElement();

    return xmlString;
}

bool UfwClient::isCurrentlyLoaded() const
{
    QProcess process;
    const QString name = "systemctl";
    const QStringList args = {"status", "ufw"};

    process.start(name, args);
    process.waitForFinished();

    // systemctl returns 0 for status if the app is loaded, and 3 otherwise.
    qDebug() << "Ufw is loaded?" << (process.exitCode() == EXIT_SUCCESS);

    return process.exitCode() == EXIT_SUCCESS;
}

bool UfwClient::supportsRuleUpdate() const
{
    return false;
}

QString UfwClient::version() const
{
    QProcess process;
    QStringList args = {"--version"};

    process.start(executablePath(), args);
    process.waitForFinished();

    if (process.exitCode() != EXIT_SUCCESS) {
        return i18n("Error fetching information from the firewall.");
    }

    return process.readAllStandardOutput();
}


#include "ufwclient.moc"

