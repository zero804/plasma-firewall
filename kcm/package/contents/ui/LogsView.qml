
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

import QtQuick 2.0
import QtQuick.Layouts 1.3
import QtQuick.Controls 2.12 as QQC2
import QtQuick.Controls 1.4 as QQC1

import org.kde.kcm 1.2 as KCM

KCM.ScrollViewKCM {
    id: root
    property var drawer: null
    property var firewallClient: null
    property var ruleEdit: null
    title: i18n("Firewall Logs")

    view:  Flickable {
        QQC1.TableView {
            id: tableView
            width: parent.width
            height: parent.height

            model: firewallClient ? firewallClient.logs() : null
            QQC1.TableViewColumn {
                title: i18n("Protocol")
                role: "protocol"
                width: 80
            }
            QQC1.TableViewColumn {
                title: i18n("From")
                role: "sourceAddress"
                width: 100
            }
            QQC1.TableViewColumn {
                role: "sourcePort"
                width: 50
            }
            QQC1.TableViewColumn {
                title: i18n("To")
                role: "destinationAddress"
                width: 100
            }
            QQC1.TableViewColumn {
                role: "destinationPort"
                width: 50
            }
            QQC1.TableViewColumn {
                title: i18n("Interface")
                role: "interface"
                width: 100
            }
        }
    }

    footer: RowLayout {
        QQC2.Button {
            text: i18n("Blacklist Log Entry")
            enabled: tableView.currentRow !== -1
            onClicked: {
                var  model = TableView.model
                var rule = firewallClient.createRuleFromLog(
                    model.data2(tableView.currentRow, "protocol"),
                    model.data2(tableView.currentRow, "sourceAddress"),
                    model.data2(tableView.currentRow, "sourcePort"),
                    model.data2(tableView.currentRow, "destinationAddress"),
                    model.data2(tableView.currentRow, "destinationPort"),
                    model.data2(tableView.currentRow, "interface")
                );
                firewallClient.addRule(rule)
            }
        }
        Item {
            Layout.fillWidth: true
        }
    }
}
