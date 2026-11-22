// SPDX-License-Identifier: GPL-3.0-only

#pragma once

namespace sdlagent
{

enum class Lang
{
    Zh,
    En
};

//[UI strings]
//Every user-visible label lives here so a second language can be added
//without touching widget code. Add a field, then fill both tables.
struct Strings
{
    const char* appTitle;
    const char* newChat;
    const char* untitled;
    const char* noSessions;

    //sidebar
    const char* pinned;
    const char* settings;
    const char* rename;
    const char* remove;
    const char* pin;
    const char* unpin;
    const char* exportSession;
    const char* exportFilter;
    const char* exportDone;
    const char* exportFailed;

    //chat
    const char* send;
    const char* stop;
    const char* statusReady;
    const char* statusThinking;
    const char* statusReplying;
    const char* statusTool;
    const char* statusStopping;
    const char* statusCancelled;
    const char* emptyChatTitle;
    const char* emptyChatHint;
    const char* noApiHint;

    //attachments
    const char* attach;
    const char* attachText;
    const char* attachImage;
    const char* attachInvalid;
    const char* attachTooLarge;
    const char* attachUnsupported;
    const char* attachTextEncoding;
    const char* attachImageUnsupported;
    const char* attachTooMany;
    const char* attachLimitHint;
    const char* attachMissing;
    const char* attachFilterText;
    const char* attachFilterImage;
    const char* attachFilterAll;

    //message rendering
    const char* userLabel;
    const char* reasoningLabel;
    const char* reasoningToggle;
    const char* toolCallLabel;
    const char* toolResultLabel;
    const char* errorLabel;

    //settings
    const char* settingsTitle;
    const char* settingsApi;
    const char* settingsAppearance;
    const char* settingsCustom;
    const char* settingsAbout;
    const char* aboutAuthorPrefix;
    const char* logs;
    const char* viewLogs;
    const char* exportLogs;
    const char* logEmpty;
    const char* logExportFailed;
    const char* updateApi;
    const char* apiJsonMissing;
    const char* apiJsonMissingHint;
    const char* fetchApiJson;
    const char* downloadUrl;
    const char* useProxy;
    const char* proxyPort;
    const char* invalidUrl;
    const char* invalidPort;
    const char* startUpdate;
    const char* downloading;
    const char* downloadDone;
    const char* downloadFailed;
    const char* downloadSaved;
    const char* apiUpdateHint;
    const char* profileName;
    const char* newProfile;
    const char* deleteProfile;
    const char* saveProfile;
    const char* noProfiles;
    const char* welcomeTitle;
    const char* welcomeText;
    const char* apiUrl;
    const char* apiKey;
    const char* model;
    const char* temperature;
    const char* contextTokens;
    const char* maxTokens;
    const char* reasoningEffort;
    const char* reasoningDefault;
    const char* apiProtocol;
    const char* sessionsDir;
    const char* sessionsDirHint;
    const char* configDir;
    const char* configDirHint;
    const char* openDir;
    const char* browse;
    const char* toolMode;
    const char* toolsOff;
    const char* toolsAsk;
    const char* toolsAuto;
    const char* showReasoning;
    const char* autoTitle;
    const char* theme;
    const char* themeDark;
    const char* themeLight;
    const char* language;
    const char* languageZh;
    const char* languageEn;
    const char* languageHint;

    //font
    const char* fontSection;
    const char* fontBackend;
    const char* fontBuiltin;
    const char* fontSystem;
    const char* fontPathLabel;
    const char* fontChoose;
    const char* fontSize;
    const char* fontHint;
    const char* fontSettings;
    const char* fontDialogTitle;
    const char* fontPreview;
    const char* apply;
    const char* copy;
    const char* selectAll;

    //model catalog
    const char* selectFromCatalog;
    const char* catalogTitle;
    const char* catalogProvider;
    const char* catalogModel;
    const char* catalogSearchProvider;
    const char* catalogSearchModel;
    const char* catalogReload;
    const char* catalogCustom;
    const char* catalogCompatible;
    const char* catalogGateway;
    const char* catalogContext;
    const char* catalogOutput;
    const char* catalogTools;
    const char* catalogToolsYes;
    const char* catalogToolsNo;
    const char* catalogCost;
    const char* catalogSource;

    const char* promptFiles;
    const char* edit;
    const char* save;
    const char* cancel;
    const char* back;
    const char* saved;
    const char* invalidNumber;
    const char* copyFailed;
    const char* copiedSessions;

    //dialogs
    const char* confirmTitle;
    const char* allow;
    const char* deny;
    const char* stopRun;
    const char* renameTitle;
    const char* deleteTitle;
    const char* deleteConfirm;
    const char* deleteText;
    const char* ok;

    //runtime messages
    const char* requestFailed;
    const char* emptyResponse;
    const char* toolDenied;
    const char* toolInterrupted;
    const char* unknownTool;
    const char* tooManyRounds;
    const char* stopped;
    const char* apiMissing;
    const char* tokenUsage;
    const char* webSearchReserved;
};

const Strings& tr();
void setLang(Lang lang);
Lang currentLang();

} //namespace sdlagent
