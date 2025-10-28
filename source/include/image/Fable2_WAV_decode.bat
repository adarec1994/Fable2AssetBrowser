@echo off
CD /D "%~dp0"
mode con:cols=200 lines=50

set sep=----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

ECHO.%sep%
ECHO.                                                                                                                                              i                                                         
ECHO.                                                                                                                                              iBPi                                                      
ECHO.           rYrrrvvvLLvvvrrrrrrrrrvsJuqBI                                                                                                       iBBBBZPPSIuUuJjuukSVUU vqusUVVXbqkjUISkuvi               
ECHO.           LBBBBQgRbjuKMBBQBBQBBBBBdKJvBi                                                                                                        UQQZqIusrvsuVdgRPugBvSBBBBPQBuYBBBBBBBBBBQi            
ECHO.            iDBkvui iSRBBBBQQQBBBBQMBB BU                                                                                                          vPRBBQgVbBBBJiiPQRu iPBvbIi XQMEqkJvvvUgBv           
ECHO.              BivsiiBBPjrii   iivSZirBrBs       sSPPQBMu          Jvrrvvvriiiiiii          isrrvvvvrrrri           UvrvLsYvrrrrrrvvvsId                ivuKqQuQsivMI    iBiqUiuBI           i           
ECHO.              Bivu iBu               QkBL     rBvr gQgQBBv        QBBZgkvudgBBBBBBBgi      iBBBgMMgBBBBBBQu        BBgsuvLJVdRQZBBBMRjsI                    dirrivRv     BLks vQr                       
ECHO.              BvvkiiBL               PDBv    iB LBYiBi  ir         ugii vQBRgEEDBQqBBP       MUi  gRbSkuJuEu        dI   IQBQgDZEDgQZUrRi                   QiiiivRv     Brqk rQr                       
ECHO.              BuSZriBv               ZBgi    B uBBBivS              Qvr MZi     rbiv QU      Iqi iQu                rqi idbi      irisKgi                   RrvrvvRL     Qisu vQr                       
ECHO.              BkvS  Bj               BqV    B vBIkBQ QX             Bir Zu      uv   QRi     udi iBv                vqi idv          ugZ                    QiviiiQL    iQiuv vBr                       
ECHO.              BvigBPBZEDRQMPui      iiLi   BiiBP  kBPiQL            Bii Rdi iijgv vKRZv      UKi iBv                vqi  BX          UII                    RivvirBL    iQiJu rQr                       
ECHO.              BriJvi YSdMBBBBBBQi         BL BX    PBI Bi          iBri qBBBBBMukggRBP       IXiivBv         qk     vP JuVjKMMMPr    ivi                    givrirMY    igiJL vQr                       
ECHO.              Bvvr  BBBREqVkUVPQBX       gP BBSiii  PBsiBi          Bi  ggusuSERBBBZEBBr     IUi iBv          RB    vDii vKgQMQBBQv                         QiiiiiRs    iBiYv vQr                       
ECHO.              BurkiiBSi          ii     bR  uVdBBBBbvRBsiB         iQi  ZX       iUBXDiBv    Sk   Bv           BE    Rvi ZDLiiiiivji                        M ri iQs    iBivv rQr                       
ECHO.              BrrSiiBs                 dg ikQBMdXVSPqVBgirB         Qri gU        iP i vBi   Usi iQv           Uqu   ZPv uk                                 Z rv iQs    iDirr iQr                       
ECHO.              Bviui BU                gB iBBki        iRKvVB        BiVuBBi   irkDBv   MBr   Jv irBJ        u rIqX   iBBqLBv           r                    b rv  Qs    iQivi iEi                       
ECHO.              BviJi BU               BS  BQi          rP viqBi     Mk IQkuVSXRBBBbi iUBBk   iQrirYKPKUuuuKBBQVu KK    iQBbudgkriiiiiLPPvi                   Riii iMv     R iZ vBr                       
ECHO.              Bvvi iBu             iQY  iSBgv        JBuvuJjgBv   BBXqqKUjJkKKKXPbDBBBgr   vBBXdIkuuqEDBgbEZQBSvEP     iqgPuIVPgQQgRQbUji      r           VV iviUBJ    uK iMBBBBRPji                   
ECHO.              Bvr  uBs           iISiiKBBBQZIi        rMBQQRgQgi  iXQQRMRQQQRMMggZPkvi      iZQQQQRRRMMgggDEZDQBBb       iUZQQQQQQQgqsi        iMBDIjJuIqqZv  iiikbQJ rBJ        iJXgBRBSi              
ECHO.              Brr  BQr      irvubZuuBBBdvi                                                                                    iiii               uQBBBBQgPbdggDMggZdBUqDdQQggZPPbgDZZbPBBBBi            
ECHO.             SBr  XBq       rQBBMRBBPr                                                                                                             irvsuUkkIIVIkUuuuUsirkkkkUkkUUuuUkVKPdgBBk           
ECHO.             Bj  kBMi         iLUJi                                                                                                                                                       ivgJ          
ECHO.            Qg  ZBMi                                                                                                                                                                                    
ECHO.           dBisBBPi                                                                                                                                                                                     
ECHO.          BBEBBDr                                                                                                                                                                                       
ECHO.        vQPKMXi                                                                                                                                                                                         
ECHO.          ii                                                                                                                                                                                           

FOR %%F IN ("%~dp1*.wav") DO (
	FOR /F "tokens=6" %%H IN ('hexed.exe -d 0 4 "%%~F"') DO (
		IF "%%H"=="xma." (
			ECHO.%sep%
			ECHO.
			
			ECHO.%~dp1%%~nxF - Deleting xma. header
			hexed.exe -r 0 4 "%%~F"
			ECHO.%~dp1%%~nxF - xma. header was deleted
			
			ECHO.%~dp1%%~nxF - Renaming to %%~nF.xma
			RENAME "%%~F" "%%~nF.xma"
			ECHO.%~dp1%%~nxF - Was renamed to %%~nF.xma
			
			ECHO.%~dp1%%~nF.xma - Decoding, using towav.exe
			towav.exe "%~dp1%%~nF.xma" >NUL
			ECHO.%~dp1%%~nF.xma - Decoded to %~dp0%%~nF.wav
			
			ECHO.Deleting of %~dp1%%~nF.xma file
			DEL /Q "%%~dpnF.xma"
			ECHO.%~dp1%%~nF.xma file was deleted
			
			ECHO.%%~nF.wav - Copying from %~dp0 to %~dp1 folder
			xcopy /q "%~dp0%%~nF.wav" "%~dp1%%~nF.wav*" >NUL
			ECHO.%%~nF.wav file was copied from %~dp0 to %~dp1 folder
			
			ECHO.Deleting of %~dp0%%~nF.wav file
			DEL /Q "%~dp0%%~nF.wav"
			ECHO.%~dp0%%~nF.wav file was deleted
			
			ECHO.			
		)
	)
)
ECHO.%sep%
ECHO.Done
pause