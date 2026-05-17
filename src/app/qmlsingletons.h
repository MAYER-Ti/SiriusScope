#ifndef QMLSINGLETONS_H
#define QMLSINGLETONS_H

#include "antennacontrollerstub.h"
#include "appstate.h"
#include "bandconfigcontroller.h"
#include "bandlistmodel.h"
#include "diagnosticsservice.h"
#include "frequencygridmodel.h"
#include "frequencyviewportmodel.h"
#include "scancontroller.h"
#include "spectrumcontrollerstub.h"
#include "spectrumdecimator.h"
#include "statusmodel.h"
#include "waterfallcontroller.h"

#include <QJSEngine>
#include <QQmlEngine>
#include <QtQml/qqmlregistration.h>

namespace siriusscope::app {

struct AppStateQmlSingleton
{
    Q_GADGET
    QML_FOREIGN(AppState)
    QML_NAMED_ELEMENT(AppState)
    QML_SINGLETON

public:
    inline static AppState *instance = nullptr;

    static AppState *create(QQmlEngine *, QJSEngine *engine)
    {
        Q_ASSERT(instance);
        engine->setObjectOwnership(instance, QJSEngine::CppOwnership);
        return instance;
    }
};

struct FrequencyViewportModelQmlSingleton
{
    Q_GADGET
    QML_FOREIGN(FrequencyViewportModel)
    QML_NAMED_ELEMENT(FrequencyViewportModel)
    QML_SINGLETON

public:
    inline static FrequencyViewportModel *instance = nullptr;

    static FrequencyViewportModel *create(QQmlEngine *, QJSEngine *engine)
    {
        Q_ASSERT(instance);
        engine->setObjectOwnership(instance, QJSEngine::CppOwnership);
        return instance;
    }
};

struct FrequencyGridModelQmlSingleton
{
    Q_GADGET
    QML_FOREIGN(FrequencyGridModel)
    QML_NAMED_ELEMENT(FrequencyGridModel)
    QML_SINGLETON

public:
    inline static FrequencyGridModel *instance = nullptr;

    static FrequencyGridModel *create(QQmlEngine *, QJSEngine *engine)
    {
        Q_ASSERT(instance);
        engine->setObjectOwnership(instance, QJSEngine::CppOwnership);
        return instance;
    }
};

struct SpectrumControllerQmlSingleton
{
    Q_GADGET
    QML_FOREIGN(SpectrumControllerStub)
    QML_NAMED_ELEMENT(SpectrumController)
    QML_SINGLETON

public:
    inline static SpectrumControllerStub *instance = nullptr;

    static SpectrumControllerStub *create(QQmlEngine *, QJSEngine *engine)
    {
        Q_ASSERT(instance);
        engine->setObjectOwnership(instance, QJSEngine::CppOwnership);
        return instance;
    }
};

struct SpectrumDecimatorQmlSingleton
{
    Q_GADGET
    QML_FOREIGN(SpectrumDecimator)
    QML_NAMED_ELEMENT(SpectrumDecimator)
    QML_SINGLETON

public:
    inline static SpectrumDecimator *instance = nullptr;

    static SpectrumDecimator *create(QQmlEngine *, QJSEngine *engine)
    {
        Q_ASSERT(instance);
        engine->setObjectOwnership(instance, QJSEngine::CppOwnership);
        return instance;
    }
};

struct WaterfallControllerQmlSingleton
{
    Q_GADGET
    QML_FOREIGN(WaterfallController)
    QML_NAMED_ELEMENT(WaterfallController)
    QML_SINGLETON

public:
    inline static WaterfallController *instance = nullptr;

    static WaterfallController *create(QQmlEngine *, QJSEngine *engine)
    {
        Q_ASSERT(instance);
        engine->setObjectOwnership(instance, QJSEngine::CppOwnership);
        return instance;
    }
};

struct AntennaControllerQmlSingleton
{
    Q_GADGET
    QML_FOREIGN(AntennaControllerStub)
    QML_NAMED_ELEMENT(AntennaController)
    QML_SINGLETON

public:
    inline static AntennaControllerStub *instance = nullptr;

    static AntennaControllerStub *create(QQmlEngine *, QJSEngine *engine)
    {
        Q_ASSERT(instance);
        engine->setObjectOwnership(instance, QJSEngine::CppOwnership);
        return instance;
    }
};

struct ScanControllerQmlSingleton
{
    Q_GADGET
    QML_FOREIGN(ScanController)
    QML_NAMED_ELEMENT(ScanController)
    QML_SINGLETON

public:
    inline static ScanController *instance = nullptr;

    static ScanController *create(QQmlEngine *, QJSEngine *engine)
    {
        Q_ASSERT(instance);
        engine->setObjectOwnership(instance, QJSEngine::CppOwnership);
        return instance;
    }
};

struct BandListModelQmlSingleton
{
    Q_GADGET
    QML_FOREIGN(BandListModel)
    QML_NAMED_ELEMENT(BandListModel)
    QML_SINGLETON

public:
    inline static BandListModel *instance = nullptr;

    static BandListModel *create(QQmlEngine *, QJSEngine *engine)
    {
        Q_ASSERT(instance);
        engine->setObjectOwnership(instance, QJSEngine::CppOwnership);
        return instance;
    }
};

struct BandConfigControllerQmlSingleton
{
    Q_GADGET
    QML_FOREIGN(BandConfigController)
    QML_NAMED_ELEMENT(BandConfigController)
    QML_SINGLETON

public:
    inline static BandConfigController *instance = nullptr;

    static BandConfigController *create(QQmlEngine *, QJSEngine *engine)
    {
        Q_ASSERT(instance);
        engine->setObjectOwnership(instance, QJSEngine::CppOwnership);
        return instance;
    }
};

struct DiagnosticsServiceQmlSingleton
{
    Q_GADGET
    QML_FOREIGN(DiagnosticsService)
    QML_NAMED_ELEMENT(DiagnosticsService)
    QML_SINGLETON

public:
    inline static DiagnosticsService *instance = nullptr;

    static DiagnosticsService *create(QQmlEngine *, QJSEngine *engine)
    {
        Q_ASSERT(instance);
        engine->setObjectOwnership(instance, QJSEngine::CppOwnership);
        return instance;
    }
};

struct StatusModelQmlSingleton
{
    Q_GADGET
    QML_FOREIGN(StatusModel)
    QML_NAMED_ELEMENT(StatusModel)
    QML_SINGLETON

public:
    inline static StatusModel *instance = nullptr;

    static StatusModel *create(QQmlEngine *, QJSEngine *engine)
    {
        Q_ASSERT(instance);
        engine->setObjectOwnership(instance, QJSEngine::CppOwnership);
        return instance;
    }
};

} // namespace siriusscope::app

#endif // QMLSINGLETONS_H
