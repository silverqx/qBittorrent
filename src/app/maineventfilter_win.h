#ifndef MAINEVENTFILTER_H
#define MAINEVENTFILTER_H

#include <QAbstractNativeEventFilter>

class MainEventFilter final : public QAbstractNativeEventFilter
{
public:
    explicit MainEventFilter();

    bool nativeEventFilter(const QByteArray &eventType, void *message, long *result) override;
};

#endif // MAINEVENTFILTER_H
