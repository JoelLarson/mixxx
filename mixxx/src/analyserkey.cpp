#include <QtDebug>
#include <QVector>

#include "analyserkey.h"
#include "track/key_preferences.h"
#include "proto/keys.pb.h"
#include "track/keyfactory.h"

using mixxx::track::io::key::ChromaticKey;
using mixxx::track::io::key::ChromaticKey_IsValid;

AnalyserKey::AnalyserKey(ConfigObject<ConfigValue> *_config)
        : m_pConfig(_config),
          m_pVamp(NULL),
          m_iSampleRate(0),
          m_iTotalSamples(0),
          m_bShouldAnalyze(false) {
}

AnalyserKey::~AnalyserKey(){
    delete m_pVamp;
}

// TODO(XXX): Get rid of the horrible duplication here between initialise and
// loadStored.
bool AnalyserKey::initialise(TrackPointer tio, int sampleRate, int totalSamples) {
    m_bShouldAnalyze = false;

    if (totalSamples == 0) {
        return false;
    }

    m_bPreferencesKeyDetectionEnabled = static_cast<bool>(
        m_pConfig->getValueString(
            ConfigKey(KEY_CONFIG_KEY, KEY_DETECTION_ENABLED)).toInt());
    if (!m_bPreferencesKeyDetectionEnabled) {
        qDebug() << "Key detection is deactivated";
        m_bShouldAnalyze = false;
        return false;
    }

    m_bPreferencesFastAnalysisEnabled = static_cast<bool>(
        m_pConfig->getValueString(
            ConfigKey(KEY_CONFIG_KEY, KEY_FAST_ANALYSIS)).toInt());
    m_bPreferencesReanalyzeEnabled = static_cast<bool>(
        m_pConfig->getValueString(
            ConfigKey(KEY_CONFIG_KEY, KEY_REANALYZE_WHEN_SETTINGS_CHANGE)).toInt());

    QString library = m_pConfig->getValueString(
        ConfigKey(VAMP_CONFIG_KEY, VAMP_ANALYSER_KEY_LIBRARY));
    QString pluginID = m_pConfig->getValueString(
        ConfigKey(VAMP_CONFIG_KEY, VAMP_ANALYSER_KEY_PLUGIN_ID));

    // TODO(rryan): This belongs elsewhere.
    if (library.isEmpty() || library.isNull())
        library = "libmixxxminimal";

    // TODO(rryan): This belongs elsewhere.
    if (pluginID.isEmpty() || pluginID.isNull())
        pluginID="qm-keydetector:2";

    m_pluginId = pluginID;
    m_iSampleRate = sampleRate;
    m_iTotalSamples = totalSamples;

    const Keys& keys = tio->getKeys();
    if (keys.isValid()) {
        QString version = keys.getVersion();
        QString subVersion = keys.getSubVersion();

        QHash<QString, QString> extraVersionInfo = getExtraVersionInfo(
            m_pluginId, m_bPreferencesFastAnalysisEnabled);
        QString newVersion = KeyFactory::getPreferredVersion();
        QString newSubVersion = KeyFactory::getPreferredSubVersion(extraVersionInfo);

        if (version == newVersion && subVersion == newSubVersion) {
            // If the version and settings have not changed then if the world is
            // sane, re-analyzing will do nothing.
            m_bShouldAnalyze = false;
            qDebug() << "Keys version/sub-version unchanged since previous analysis. Not analyzing.";
        } else if (m_bPreferencesReanalyzeEnabled) {
            m_bShouldAnalyze = true;
        } else {
            m_bShouldAnalyze = false;
            qDebug() << "Track has previous key detection result that is not up"
                     << "to date with latest settings but user preferences"
                     << "indicate we should not re-analyze it.";
        }
    } else {
        // If we got here, we think we may want to analyze this track.
        m_bShouldAnalyze = true;
    }

    if (!m_bShouldAnalyze) {
        qDebug() << "Key calculation will not start.";
        return false;
    }

    qDebug() << "Key calculation started with plugin" << m_pluginId;
    m_pVamp = new VampAnalyser(m_pConfig);
    m_bShouldAnalyze = m_pVamp->Init(
        library, m_pluginId, sampleRate, totalSamples,
        m_bPreferencesFastAnalysisEnabled);

    if (!m_bShouldAnalyze) {
        delete m_pVamp;
        m_pVamp = NULL;
    }

    return m_bShouldAnalyze;
}

// TODO(XXX): Get rid of the horrible duplication here between initialise and
// loadStored.
bool AnalyserKey::loadStored(TrackPointer tio) const {
    bool bPreferencesFastAnalysisEnabled = static_cast<bool>(
        m_pConfig->getValueString(
            ConfigKey(KEY_CONFIG_KEY, KEY_FAST_ANALYSIS)).toInt());

    QString library = m_pConfig->getValueString(
        ConfigKey(VAMP_CONFIG_KEY, VAMP_ANALYSER_KEY_LIBRARY));
    QString pluginID = m_pConfig->getValueString(
        ConfigKey(VAMP_CONFIG_KEY, VAMP_ANALYSER_KEY_PLUGIN_ID));

    // TODO(rryan): This belongs elsewhere.
    if (library.isEmpty() || library.isNull())
        library = "libmixxxminimal";

    // TODO(rryan): This belongs elsewhere.
    if (pluginID.isEmpty() || pluginID.isNull())
        pluginID="qm-keydetector:2";

    const Keys& keys = tio->getKeys();
    if (keys.isValid()) {
        QString version = keys.getVersion();
        QString subVersion = keys.getSubVersion();

        QHash<QString, QString> extraVersionInfo = getExtraVersionInfo(
            pluginID, bPreferencesFastAnalysisEnabled);
        QString newVersion = KeyFactory::getPreferredVersion();
        QString newSubVersion = KeyFactory::getPreferredSubVersion(extraVersionInfo);

        if (version == newVersion && subVersion == newSubVersion) {
            // If the version and settings have not changed then if the world is
            // sane, re-analyzing will do nothing.
            qDebug() << "Keys version/sub-version unchanged since previous analysis. Not analyzing.";
            return true;
        } else if (m_bPreferencesReanalyzeEnabled) {
            return false;
        } else {
            qDebug() << "Track has previous key detection result that is not up"
                     << "to date with latest settings but user preferences"
                     << "indicate we should not re-analyze it.";
            return true;
        }
    } else {
        // If we got here, we want to analyze this track.
        return false;
    }
}

void AnalyserKey::process(const CSAMPLE *pIn, const int iLen) {
    if (!m_bShouldAnalyze || m_pVamp == NULL)
        return;
    m_bShouldAnalyze = m_pVamp->Process(pIn, iLen);
    if (!m_bShouldAnalyze) {
        delete m_pVamp;
        m_pVamp = NULL;
    }
}

void AnalyserKey::cleanup(TrackPointer tio) {
    Q_UNUSED(tio);
    delete m_pVamp;
    m_pVamp = NULL;
}

void AnalyserKey::finalise(TrackPointer tio) {
    if (!m_bShouldAnalyze || m_pVamp == NULL) {
        return;
    }

    bool success = m_pVamp->End();
    qDebug() << "Key Detection" << (success ? "complete" : "failed");

    QVector<double> frames = m_pVamp->GetInitFramesVector();
    QVector<double> keys = m_pVamp->GetLastValuesVector();
    delete m_pVamp;
    m_pVamp = NULL;

    if (frames.size() == 0 || frames.size() != keys.size()) {
        qWarning() << "AnalyserKey: Key sequence and list of times do not match.";
        return;
    }

    KeyChangeList key_changes;
    for (int i = 0; i < keys.size(); ++i) {
        if (ChromaticKey_IsValid(keys[i])) {
            key_changes.push_back(qMakePair(
                static_cast<ChromaticKey>(keys[i]), frames[i]));
        }
    }

    QHash<QString, QString> extraVersionInfo = getExtraVersionInfo(
        m_pluginId, m_bPreferencesFastAnalysisEnabled);
    Keys track_keys = KeyFactory::makePreferredKeys(
        tio, key_changes, extraVersionInfo,
        m_iSampleRate, m_iTotalSamples);
    tio->setKeys(track_keys);
}

// static
QHash<QString, QString> AnalyserKey::getExtraVersionInfo(
    QString pluginId, bool bPreferencesFastAnalysis) {
    QHash<QString, QString> extraVersionInfo;
    extraVersionInfo["vamp_plugin_id"] = pluginId;
    if (bPreferencesFastAnalysis) {
        extraVersionInfo["fast_analysis"] = "1";
    }
    return extraVersionInfo;
}
