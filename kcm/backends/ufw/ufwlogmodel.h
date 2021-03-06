#ifndef UFWLOGMODEL_H
#define UFWLOGMODEL_H

#include "loglistmodel.h"

class UfwLogModel : public LogListModel {
    Q_OBJECT
public:
    UfwLogModel(QObject *parent);
    void addRawLogs(const QStringList &rawLogsList) override;
};

#endif
