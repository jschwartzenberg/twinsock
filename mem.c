void
memset(char *pvMem, int iVal, int nBytes)
{
	while(nBytes--)
		*pvMem++ = iVal;
}

void
memcpy(char *pvDest, char *pvSource, int nBytes)
{
	while (nBytes--)
		*pvDest++ = *pvSource++;
}
