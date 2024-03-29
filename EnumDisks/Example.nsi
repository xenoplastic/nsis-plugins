#
# NSIS 3.x 支持
#

Unicode true

!ifdef NSIS_PACKEDVERSION
!define /math NSIS_VERSON_MAJOR ${NSIS_PACKEDVERSION} >> 24
!else
!define NSIS_VERSON_MAJOR 2
!endif

!if ${NSIS_VERSON_MAJOR} > 2

#
# NSIS 3.x 方式引用插件
#
!addplugindir "/x86-unicode" ".\x86-unicode"
!addplugindir "/x86-ansi" ".\x86-ansi"

!else

#
# NSIS 2.x 方式引用插件
#
!ifdef NSIS_UNICODE
!addplugindir ".\x86-unicode"
!else
!addplugindir ".\x86-ansi"
!endif

!endif

#
# EnumDisks Example
#
Name "EnumDisks"
Outfile "EnumDisks.exe"
Caption "$(^Name)"

XPStyle on
ShowInstDetails show
RequestExecutionLevel user
InstallColors /windows

Page instfiles

!macro EnumDisks enum_

	EnumDisks::Init "${enum_}"
	EnumDisks::Count
	Pop $R0

	StrCpy $0 0
lbl_loop:
	IntCmp $0 $R0 lbl_done

	DetailPrint "========================================"

	EnumDisks::GetEnumerator $0
	Pop $1
	DetailPrint "枚举类型：[$1]"

	EnumDisks::GetFriendlyName $0
	Pop $1
	DetailPrint "友好名称：[$1]"

	EnumDisks::GetSerialNumber $0
	Pop $1
	DetailPrint "设备序列：[$1]"

	EnumDisks::GetProductID $0
	Pop $1
	DetailPrint "产品标识：[$1]"

	EnumDisks::GetVendorID $0
	Pop $1
	DetailPrint "厂商标识：[$1]"

	EnumDisks::GetHealthStatus $0
	Pop $1
	DetailPrint "健康状态：[$1]"

	EnumDisks::GetSeekPenalty $0
	Pop $1
	DetailPrint "查找开销：[$1]"

	EnumDisks::GetTrimEnabled $0
	Pop $1
	DetailPrint "启用优化：[$1]"

	EnumDisks::GetDiskSize $0
	Pop $1
	DetailPrint "磁盘大小：[$1]"

	IntOp $0 $0 + 1
	Goto lbl_loop
lbl_done:

	EnumDisks::Clear

!macroend

Section "USBSTOR"

	DetailPrint "########################################"
	DetailPrint "#### USBSTOR"
	DetailPrint "########################################"

	!insertmacro EnumDisks "USBSTOR"

SectionEnd

Section "SCSI"

	DetailPrint "########################################"
	DetailPrint "#### SCSI"
	DetailPrint "########################################"

	!insertmacro EnumDisks "SCSI"

SectionEnd

Section "ALL"

	DetailPrint "########################################"
	DetailPrint "#### ALL"
	DetailPrint "########################################"

	!insertmacro EnumDisks "*"

SectionEnd