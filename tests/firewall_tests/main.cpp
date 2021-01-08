#include <QDebug>
#include <QCoreApplication>

#include "firewallclient.h"
#include "rulelistmodel.h"
#include "rule.h"

#include <KJob>

// Start
void testDisableClient(FirewallClient* client);
void testDisableClientResult(FirewallClient* client, KJob *job);

// Second Method.
void testEnableClient(FirewallClient* client);
void testEnableClientResult(FirewallClient* client, KJob *job);

// Third Method
void testCurrentRulesEmpty(FirewallClient* client);
void testCurrentRulesEmptyResult(FirewallClient* client);

// Fourth Method
void testAddRules(FirewallClient* client);
void testAddRulesResult(FirewallClient* client, KJob *job);

// Fifth Method
// void testRemoveRules(FirewallClient* client);
// void testRemoveRulesResult(FirewallClient* client, KJob *job);

void printHelp() {
    qDebug() << "Usage Information:";
    qDebug() << "$" << "firewall_test backend_name";
    qDebug() << "Where backend name is ether ufw or firewalld.";
    qDebug() << "Make sure you have no firewall running";
    qDebug() << "And make sure you have no profile configured on either firewall.";
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "argc" << argc;
    if (argc != 2) {
        qDebug() << "Invalid number of arguments, please use:";
        printHelp();
        exit(1);
    }

    QString firewallBackendName = argv[1];
    if (firewallBackendName != "ufw" && firewallBackendName != "firewalld") {
        printHelp();
        exit(1);
    }

    qDebug() << firewallBackendName << "test called";

    auto *client = new FirewallClient();
    client->setBackend({firewallBackendName});

    qDebug() << "Backend Loaded" << client->backend() << "expected" << firewallBackendName;

    // Initial backend state.
    qDebug() << "Client Enabled?" << client->enabled();

    // Start trying to disable, the order of calls is the same as the definition order.
    // Please don't change as this is really annoying to test.
    testDisableClient(client);

    return app.exec();
}

void testDisableClient(FirewallClient* client) {
    // From here on, We will jump thru the usage via connects.
    KJob *enableJob = client->setEnabled(false);

    // Ignore, we are already disabled.
    if (enableJob == nullptr) {
        testEnableClient(client);
        return;
    }

    QObject::connect(enableJob, &KJob::result, [client, enableJob]{ testDisableClientResult(client, enableJob); });
    enableJob->start();
}

void testDisableClientResult(FirewallClient *client, KJob *job) {
    if (job->error() != KJob::NoError) {
        qDebug() << "Error disabling the client, aborting." << client->enabled();
        qDebug() << job->errorString();
        exit(1);
    }

    qDebug() << "Disable client, expected: False, got:" << client->enabled();
    testEnableClient(client);
}

void testEnableClient(FirewallClient* client) {
    // From here on, We will jump thru the usage via connects.
    KJob *enableJob = client->setEnabled(true);

    // Ignore, we are already enabled.
    if (enableJob == nullptr) {
        testCurrentRulesEmpty(client);
        return;
    }

    QObject::connect(enableJob, &KJob::result, [client, enableJob]{ testEnableClientResult(client, enableJob); });
    enableJob->start();
}

void testEnableClientResult(FirewallClient *client, KJob *job) {
    if (job->error() != KJob::NoError) {
        qDebug() << "Error disabling the client, aborting." << client->enabled();
        qDebug() << job->errorString();
        exit(1);
    }

    qDebug() << "Enable client, expected: True, got:" << client->enabled();
    testCurrentRulesEmpty(client);
}

void testCurrentRulesEmpty(FirewallClient* client)
{
    if (client->rulesModel()->rowCount() != 0) {
        qDebug() << "We need a clean firewall rules to do the testing, please backup your rules and try again.";
        exit(1);
    }

    qDebug() << "Number of current rules, Expected: 0, got:" << client->rulesModel()->rowCount();
    testCurrentRulesEmptyResult(client);
}

void testCurrentRulesEmptyResult(FirewallClient* client)
{
    testAddRules(client);
}


// Fourth Method
void testAddRules(FirewallClient* client) {
    qDebug() << "Known Protocols" << client->knownProtocols();
    qDebug() << "Known Interfaces" << client->knownInterfaces();

    QString interface = client->knownInterfaces()[0];

    // Expected output for firewalld
    // firewalld.client: ("-j", "DROP", "-p", "tcp", "-d", "127.0.0.1", "--dport=21", "-s", "127.0.0.1", "--sport=12")
    // firewalld.job: firewalld  "addRule" (
    //     QVariant(QString, "ipv4"),
    //     QVariant(QString, "filter"),
    //     QVariant(QString, "INPUT"),
    //     QVariant(int, 0),
    //     QVariant(QStringList, ("-j", "DROP", "-p", "tcp", "-d", "127.0.0.1", "--dport=21", "-s", "127.0.0.1", "--sport=12"))
    //  )

    // Current Output for firewalld:
    // firewalld.client: ("-j", "DROP", "-p", "tcp", "-d", "127.0.0.1", "--dport=21", "-s", "127.0.0.1", "--sport=12")
    //
    // firewalld.job: firewalld  "addRule" (
    //     QVariant(QString, "ipv4"),
    //     QVariant(QString, "filter"),
    //     QVariant(QString, "INPUT"),
    //     QVariant(int, 0),
    //     QVariant(QStringList, ("-j", "DROP", "-p", "tcp", "-d", "127.0.0.1", "--dport=21", "-s", "127.0.0.1", "--sport=12")))

    // Expected output for ufw
    // Debug message from helper: Cmd args passed to ufw: (
    // "--add=<rule action=\"deny\"
    //         direction=\"in\"
    //         dport=\"21\"
    //         sport=\"12\"
    //         protocol=\"TCP\"
    //         dst=\"127.0.0.1\"
    //         src=\"127.0.0.1\"
    //         logtype=\"\"
    //         v6=\"False\"/>")

    // Current Output for ufw
    // Debug message from helper: Cmd args passed to ufw: (
    // "--add=<rule action=\"deny\"
    //    direction=\"in\"
    //    dport=\"21\"
    //    sport=\"12\"
    //    dst=\"127.0.0.1\"
    //    src=\"127.0.0.1\"
    //    logtype=\"\"
    //    v6=\"False\"/>")

    auto *rule = new Rule(
        Types::Policy::POLICY_DENY, // Policy
        true,                        // Incomming?
        Types::Logging::LOGGING_OFF, // Logging
        0,                           // Protocol Id on knownProtocols
        "127.0.0.1",                 // Source Host
        "12",                        // Source Port
        "127.0.0.1",                 // Destination Port
        "21",                        // Destination Port
        QString(),                   // Interface In
        QString(),                   // Interface Out
        QString(),                   // Source App // Only used in UFW - Remove?
        QString(),                   // Destination App // Only used in UFW - Remove?
        0,                           // Index (TODO: Remove This)
        false);                      // IPV6?

    KJob *job = client->addRule(rule);
    QObject::connect(job, &KJob::result, [client, job]{ testAddRulesResult(client, job); });

    // TODO:
    // This job is started inside of the addRule, currently we have an inconsistency on what's
    // started by default and what's started by Qml. We need to fix this.
    // job->start();
}

void testAddRulesResult(FirewallClient* client, KJob *job)
{
    qDebug() << "Getting the result of the Add Rule";
    if (job->error() != KJob::NoError) {
        qDebug() << "Add rules failed" << job->errorString();
        exit(1);
    }

    qDebug() << "Add Rules succeeded, number of rules:" << client->rulesModel()->rowCount();
}

