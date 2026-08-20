# KawaiiPhysics.locmeta を LocMeta v1 (AddedCompiledCultures) へダウングレードする。
#
# 背景: UE 5.7 以降の GatherText は LocMeta v2 (AddedIsUGC) で書き出すが、
# UE 5.6 以前のエンジンは v1 までしか読めず、プラグイン導入先で
# 「LocMeta 'KawaiiPhysics' is too new to be loaded (File Version: 2, Loader Version: 1)」
# エラーになり訳が読み込まれない。v2 の追加フィールドは末尾の bIsUGC (bool, 4 bytes) のみで、
# プラグイン配布物では常に false のため、v1 へ落としても情報の損失はない（5.7/5.8 も v1 を読める）。
#
# 運用: UE 5.7+ で GatherText を実行するたびに v2 で再生成されるため、gather 後は必ず本スクリプトを実行すること。
# 使い方: powershell -ExecutionPolicy Bypass -File Tools/downgrade_locmeta.ps1

param(
	[string]$Path = (Join-Path $PSScriptRoot "..\Plugins\KawaiiPhysics\Content\Localization\KawaiiPhysics\KawaiiPhysics.locmeta")
)

$ErrorActionPreference = "Stop"
$Path = (Resolve-Path $Path).Path
$Bytes = [System.IO.File]::ReadAllBytes($Path)

# フォーマット: magic GUID (16 bytes) + version (1 byte) + ペイロード
if ($Bytes.Length -lt 21)
{
	throw "locmeta が短すぎます (${($Bytes.Length)} bytes): $Path"
}

$Version = $Bytes[16]
if ($Version -eq 1)
{
	Write-Host "既に v1 です。変更なし: $Path"
	return
}
if ($Version -ne 2)
{
	throw "想定外の LocMeta バージョン $Version です（v2 のみダウングレード可能）: $Path"
}

# v2 の追加フィールドは末尾の bIsUGC (bool = 4 bytes)。false 以外なら情報が失われるため中断する
$Tail = $Bytes[($Bytes.Length - 4)..($Bytes.Length - 1)]
if (($Tail -join ",") -ne "0,0,0,0")
{
	throw "bIsUGC が false ではありません（末尾: $($Tail -join ' ')）。ダウングレードを中断します: $Path"
}

$Out = $Bytes[0..($Bytes.Length - 5)]
$Out[16] = 1
[System.IO.File]::WriteAllBytes($Path, $Out)
Write-Host "v2 -> v1 へダウングレードしました ($($Bytes.Length) -> $($Out.Length) bytes): $Path"
