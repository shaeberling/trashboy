
#include "settings.h"


nvs_handle SettingsBase::storage;


/* Init */
void init_settings()
{
  SettingsBase::init();
  settingsScreen.init();
  settingsROM.init();
  settingsTrsIO.init();
#if 0
  settingsSplashScreen.init();
  settingsCalibration.init();
#endif
}
