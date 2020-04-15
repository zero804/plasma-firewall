/*
 * Copyright 2018 Alexis Lopes Zubeta <contact@azubieta.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License or (at your option) version 3 or any later version
 * accepted by the membership of KDE e.V. (or its successor approved
 * by the membership of KDE e.V.), which shall act as a proxy
 * defined in Section 14 of version 3 of the license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef FIREWALLDCLIENT_H
#define FIREWALLDCLIENT_H

#include <QString>
#include <QTimer>
#include <QtDBus/QDBusMessage>

#include <ifirewallclientbackend.h>
#include <profile.h>

class RuleListModel;
class LogListModel;

struct firewalld_reply {
    QString ipv;
    QString table;
    QString chain;
    int priority = 0;
    QStringList rules = {};
};

Q_DECLARE_METATYPE(firewalld_reply);

class Q_DECL_EXPORT FirewalldClient : public IFirewallClientBackend
{
    Q_OBJECT
public:
    explicit FirewalldClient(QObject *parent, const QVariantList &args);

        void refresh() override;
        RuleListModel* rules() const override;
        RuleWrapper* getRule(int index) override;
        KJob *addRule(RuleWrapper *rule) override;
        KJob *removeRule(int index) override;
        KJob *updateRule(RuleWrapper *rule) override;
        KJob *moveRule(int from, int to) override;
        KJob *queryStatus(FirewallClient::DefaultDataBehavior defaultsBehavior, FirewallClient::ProfilesBehavior profilesBehavior) override;
        KJob *setDefaultIncomingPolicy(QString defaultIncomingPolicy) override;
        KJob *setDefaultOutgoingPolicy(QString defaultOutgoingPolicy) override;
        KJob *setEnabled(const bool enabled) override;

        /* Creates a new Rule and returns it to the Qml side, passing arguments based on the Connecion Table. */
        RuleWrapper* createRuleFromConnection(
                const QString &protocol,
                const QString &localAddress,
                const QString &foreignAddres,
                const QString &status) override;

        RuleWrapper* createRuleFromLog(
                const QString &protocol,
                const QString &sourceAddress,
                const QString &sourcePort,
                const QString &destinationAddress,
                const QString &destinationPort,
                const QString &inn) override;

        bool enabled() const override;
        QString defaultIncomingPolicy() const override;
        QString defaultOutgoingPolicy() const override;
        QString name() const override;

        LogListModel* logs() override;
        bool logsAutoRefresh() const override;
        void setLogsAutoRefresh(bool logsAutoRefresh) override;
        static IFirewallClientBackend* createMethod(FirewallClient *parent);
        bool hasExecutable() const override;
        void refreshProfiles() override;

    protected slots:
        void refreshLogs();

    protected:
        void setExecutable(const bool &hasExecutable);
        QVariantList buildRule(Rule r, FirewallClient::Ipv ipvfamily = FirewallClient::IPV4);

    private:
        QString m_status;
        QStringList         m_rawLogs;
        bool                m_isBusy;
        Profile        m_currentProfile;
        RuleListModel*      m_rulesModel;
        LogListModel*       m_logs;
        QTimer              m_logsRefreshTimer;
        bool m_logsAutoRefresh;
        bool m_serviceStatus;
};

#endif // FIREWALLDCLIENT_H
