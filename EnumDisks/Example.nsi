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

Section "USBSTOR"
	DetailPrint "########################################"
	DetailPrint "#### USBSTOR"
	DetailPrint "########################################"

	EnumDisks::Init "USBSTOR"
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

	EnumDisks::GetDiskSize $0
	Pop $1
	DetailPrint "磁盘大小：[$1]"

	EnumDisks::GetSerialNumber $0
	Pop $1
	DetailPrint "设备序列：[$1]"

	EnumDisks::GetProductId $0
	Pop $1
	DetailPrint "产品标识：[$1]"

	EnumDisks::GetVendorId $0
	Pop $1
	DetailPrint "厂商标识：[$1]"

	IntOp $0 $0 + 1
	Goto lbl_loop
lbl_done:

	EnumDisks::Clear
SectionEnd

Section "SCSI"
	DetailPrint "########################################"
	DetailPrint "#### SCSI"
	DetailPrint "########################################"

	EnumDisks::Init "SCSI"
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

	EnumDisks::GetDiskSize $0
	Pop $1
	DetailPrint "磁盘大小：[$1]"

	EnumDisks::GetSerialNumber $0
	Pop $1
	DetailPrint "设备序列：[$1]"

	EnumDisks::GetProductId $0
	Pop $1
	DetailPrint "产品标识：[$1]"

	EnumDisks::GetVendorId $0
	Pop $1
	DetailPrint "厂商标识：[$1]"

	IntOp $0 $0 + 1
	Goto lbl_loop
lbl_done:

	EnumDisks::Clear
SectionEnd

Section "ALL"
	DetailPrint "########################################"
	DetailPrint "#### ALL"
	DetailPrint "########################################"

	EnumDisks::Init "*"
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

	EnumDisks::GetDiskSize $0
	Pop $1
	DetailPrint "磁盘大小：[$1]"

	EnumDisks::GetSerialNumber $0
	Pop $1
	DetailPrint "设备序列：[$1]"

	EnumDisks::GetProductId $0
	Pop $1
	DetailPrint "产品标识：[$1]"

	EnumDisks::GetVendorId $0
	Pop $1
	DetailPrint "厂商标识：[$1]"

	IntOp $0 $0 + 1
	Goto lbl_loop
lbl_done:

	EnumDisks::Clear
SectionEnd