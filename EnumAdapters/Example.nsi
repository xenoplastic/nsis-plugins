/*

物理媒体类型：

微软相关资料网站：
http://msdn.microsoft.com/en-us/library/aa814491.aspx

目前已定义值列表：

NdisPhysicalMediumUnspecified   (0) 未指定
NdisPhysicalMediumWirelessLan   (1) 无线局域网 (802.11)
NdisPhysicalMediumCableModem    (2) 有线电缆数据接口
NdisPhysicalMediumPhoneLine     (3) 家庭电话线网络
NdisPhysicalMediumPowerLine     (4) 电力线上网
NdisPhysicalMediumDSL           (5) ADSL/UADSL/SDSL
NdisPhysicalMediumFibreChannel  (6) 光纤信道网络
NdisPhysicalMedium1394          (7) IEEE 1394
NdisPhysicalMediumWirelessWan   (8) 无线广域网 (CDPD/CDMA/GSM/GPRS)
NdisPhysicalMediumNative802_11  (9) 本地无线局域网 (802.11)
NdisPhysicalMediumBluetooth    (10) 蓝牙网络
NdisPhysicalMediumInfiniband   (11) 高速互连网络
NdisPhysicalMediumWiMax        (12) 全球微波网络
NdisPhysicalMediumUWB          (13) 超级宽带
NdisPhysicalMedium802_3        (14) 以太网 (802.3)
NdisPhysicalMedium802_5        (15) 令牌环网
NdisPhysicalMediumIrda         (16) 红外网络
NdisPhysicalMediumWiredWAN     (17) 有线广域网
NdisPhysicalMediumWiredCoWan   (18) 连接导向式有线广域网
NdisPhysicalMediumOther        (19) 其他

我之前 C++ 程序输出网络设备时，判断无线网的标准是以下物理媒体类型视为无线网卡：

NdisPhysicalMediumWirelessLan   (1) 无线局域网 (802.11)
NdisPhysicalMediumWirelessWan   (8) 无线广域网 (CDPD/CDMA/GSM/GPRS)
NdisPhysicalMediumNative802_11  (9) 本地无线局域网 (802.11)
NdisPhysicalMediumBluetooth    (10) 蓝牙网络
NdisPhysicalMediumWiMax        (12) 全球微波网络
NdisPhysicalMediumUWB          (13) 超级宽带
NdisPhysicalMediumIrda         (16) 红外网络

以上判断并不一定完全涵盖所有无限上网的网卡类型，仅为个人查资料后定下判断标准。
具体可以搜索上述微软资料页面中提到的英文关键字以了解更多。

由于我们已经过滤了 PCI，实际上大部分网络类型是几乎不会出现的。

*/

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
# EnumAdapters Example
#
Name "EnumAdapters"
Outfile "EnumAdapters.exe"
Caption "$(^Name)"

XPStyle on
ShowInstDetails show
RequestExecutionLevel user
InstallColors /windows

Page instfiles

Section "PCI"
	DetailPrint "########################################"
	DetailPrint "#### PCI"
	DetailPrint "########################################"

	EnumAdapters::Init "PCI"
	EnumAdapters::Count
	Pop $R0

	StrCpy $0 0
lbl_loop:
	IntCmp $0 $R0 lbl_done

	DetailPrint "========================================"

	EnumAdapters::GetFriendlyName $0
	Pop $1
	DetailPrint "设备名称：[$1]"

	EnumAdapters::GetDescription $0
	Pop $1
	DetailPrint "设备描述：[$1]"

	EnumAdapters::GetMacAddress $0
	Pop $1
	DetailPrint "网卡地址：[$1]"

	EnumAdapters::GetMediumType $0
	Pop $1
	DetailPrint "媒体类型：[$1]"

	EnumAdapters::GetIpAddress $0
	Pop $1
	DetailPrint "IPv4地址：[$1]"

	IntOp $0 $0 + 1
	Goto lbl_loop
lbl_done:

	EnumAdapters::Clear
SectionEnd

Section "ALL"
	DetailPrint "########################################"
	DetailPrint "#### ALL"
	DetailPrint "########################################"

	EnumAdapters::Init "*"
	EnumAdapters::Count
	Pop $R0

	StrCpy $0 0
lbl_loop:
	IntCmp $0 $R0 lbl_done

	DetailPrint "========================================"

	EnumAdapters::GetFriendlyName $0
	Pop $1
	DetailPrint "设备名称：[$1]"

	EnumAdapters::GetDescription $0
	Pop $1
	DetailPrint "设备描述：[$1]"

	EnumAdapters::GetEnumerator $0
	Pop $1
	DetailPrint "枚举类型：[$1]"

	EnumAdapters::GetMacAddress $0
	Pop $1
	DetailPrint "网卡地址：[$1]"

	EnumAdapters::GetMediumType $0
	Pop $1
	DetailPrint "媒体类型：[$1]"

	EnumAdapters::GetIpAddress $0
	Pop $1
	DetailPrint "IPv4地址：[$1]"

	IntOp $0 $0 + 1
	Goto lbl_loop
lbl_done:

	EnumAdapters::Clear
SectionEnd