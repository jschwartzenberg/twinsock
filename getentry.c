/*
 *  TwinSock - "Troy's Windows Sockets"
 *
 *  Copyright (C) 1995  Troy Rollo <troy@cbme.unsw.EDU.AU>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the license in the file LICENSE.TXT included
 *  with the TwinSock distribution.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
 */
#ifdef _Windows
#include <windows.h>
#else
#include <stdio.h>
#include <ctype.h>
#endif

int
GetTwinSockSetting(	char	*pchSection,
			char	*pchItem,
			char	*pchDefault,
			char	*pchBuffer,
			int	nChars)
{
#ifdef _Windows
	return GetPrivateProfileString(	pchSection,
					pchItem,
					pchDefault,
					pchBuffer,
					nChars,
					"TWINSOCK.INI");
#else
	static	FILE	*fpIni = 0;
	static	int	iDone = 0;
	char	achBuffer[1024];
	int	iInSection = 0;
	char	*c;

	if (!iDone)
	{
		strcpy(achBuffer, getenv("HOME"));
		strcat(achBuffer, "/.twinsock");
		fpIni = fopen(achBuffer, "r");
		iDone = 1;
	}
	if (!fpIni)
	{
		strcpy(pchBuffer, pchDefault);
		return strlen(pchDefault);
	}
	rewind(fpIni);
	while (fgets(achBuffer, 1024, fpIni))
	{
		if (*achBuffer == ';' || *achBuffer == '\n')
			continue;
		if (*achBuffer == '[')
		{
			c = achBuffer + 1;
			while (isspace(*c))
				c++;
			if (strncmp(c, pchSection, strlen(pchSection)))
			{
				iInSection = 0;
				continue;
			}
			c += strlen(pchSection);
			while (isspace(*c))
				c++;
			if (*c != ']')
			{
				iInSection = 0;
				continue;
			}
			iInSection = 1;
			continue;
		}
		if (!iInSection)
			continue;
		if (strncmp(achBuffer, pchItem, strlen(pchItem)))
			continue;
		c = achBuffer + strlen(pchItem);
		while (isspace(*c))
			c++;
		if (*c++ != '=')
			continue;
		while (isspace(*c))
			c++;
		c[strlen(c)] = 0;
		strncpy(pchBuffer, c, nChars);
			pchBuffer[nChars - 1] = 0;
		pchBuffer[strlen(pchBuffer) - 1] = 0;
		return strlen(pchBuffer);
	}
	strcpy(pchBuffer, pchDefault);
	return strlen(pchDefault);
#endif
}

