#include "AGBUITheme.h"

const UAGBUITheme* UAGBUISettings::GetTheme()
{
	if (const UAGBUITheme* Theme = GetDefault<UAGBUISettings>()->Theme.LoadSynchronous())
	{
		return Theme;
	}
	return GetDefault<UAGBUITheme>();
}
