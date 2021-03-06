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

/*

#
# PathEnvUtil 使用说明
#

1. PathEnvUtil::Append

添加一个路径到 PATH 环境变量

添加时，如果待添加的路径重复出现（不区分大小写，末尾有无反斜线均视为同一路径），则只保留一个。
添加后，保留的路径出现在原值中第一次出现该路径的位置，大小写和末尾反斜线与参数指定值一致。
非参数指定的路径重复出现，不在此插件的去重范围内。

返回 0 表示成功，其他值均为错误代码。

如 PATH 环境变量原值如下：
%SystemRoot%\system32;%SystemRoot%;D:\Git\bin\;D:\Git\bin;E:\GitHub;D:\git\bin\;D:\git\bin;F:\GitLab;
添加路径 D:\Git\Bin 之后：
%SystemRoot%\system32;%SystemRoot%;D:\Git\Bin;E:\GitHub;F:\GitLab;

2. PathEnvUtil::Exists

判断一个路径是否存在于 PATH 环境变量

判断时不区分大小写，末尾有无反斜线均视为同一路径，只要出现一次就认为存在。

返回 1 表示存在，0 表示不存在。

3. PathEnvUtil::Remove

从 PATH 环境变量删除一个路径

删除时，如果待删除的路径重复出现（不区分大小写，末尾有无反斜线均视为同一路径），则全部删除。
非参数指定的路径重复出现，不在此插件的删除范围内。

返回 0 表示成功，其他值均为错误代码。

如 PATH 环境变量原值如下：
%SystemRoot%\system32;%SystemRoot%;D:\Git\bin\;D:\Git\bin;E:\GitHub;D:\git\bin\;D:\git\bin;F:\GitLab;
删除路径 D:\Git\Bin 之后：
%SystemRoot%\system32;%SystemRoot%;E:\GitHub;F:\GitLab;

*/

#
# PathEnvUtil Example
#
Name "PathEnvUtil"
Outfile "PathEnvUtil.exe"
Caption "$(^Name)"

XPStyle on
ShowInstDetails show
RequestExecutionLevel admin
InstallColors /windows

Page components
Page instfiles

Section "append" SEC_APPEND
	# 将 D:\Example\bin 添加到 PATH 环境变量
	DetailPrint "about to add $\"D:\Example\bin$\" to PATH environment..."
	PathEnvUtil::Append "D:\Example\bin"
	Pop $R0
	# $R0 - 0 表示成功，其他值代表错误代码。
	DetailPrint "result: $R0"
SectionEnd

Section "verify" SEC_VERIFY
	# 判断 D:\Example\bin 是否存在于 PATH 环境变量
	DetailPrint "check whether $\"D:\Example\bin$\" exists in PATH environment..."
	PathEnvUtil::Exists "D:\Example\bin"
	Pop $R0
	# $R0 - 1 表示存在，0 表示不存在。
	DetailPrint "result: $R0"
SectionEnd

Section "remove" SEC_REMOVE
	# 将 D:\Example\bin 从 PATH 环境变量删除
	DetailPrint "about to remove $\"D:\Example\bin$\" from PATH environment..."
	PathEnvUtil::Remove "D:\Example\bin"
	Pop $R0
	# $R0 - 0 表示成功，其他值代表错误代码。
	DetailPrint "result: $R0"
SectionEnd