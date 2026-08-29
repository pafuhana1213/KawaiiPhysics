import json
import re
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path


# READMEとローカル配布文面のUE対応バージョン、およびupluginのVersionNameと更新履歴を検査します。
# Fab系ファイルは未追跡のローカル運用ファイルなので、CI上に存在しない場合はスキップします。
@dataclass(frozen=True)
class VersionRangeCheck:
    path: str
    label: str
    pattern: re.Pattern[str]


REPO_ROOT = Path(__file__).resolve().parent.parent
MANDATORY_FILES = {
    "README.md",
    "README_en.md",
    "Plugins/KawaiiPhysics/KawaiiPhysics.uplugin",
}

VERSION_RANGE_CHECKS = [
    VersionRangeCheck(
        "README.md",
        "badge",
        re.compile(r"!\[UE Version\]\(https://img\.shields\.io/badge/Unreal%20Engine-(?P<min>\d+\.\d+)--(?P<max>\d+\.\d+)-"),
    ),
    VersionRangeCheck(
        "README.md",
        "body",
        re.compile(r"\*\*UE\s+(?P<min>\d+\.\d+)\s*~\s*(?P<max>\d+\.\d+)\*\*"),
    ),
    VersionRangeCheck(
        "README_en.md",
        "badge",
        re.compile(r"!\[UE Version\]\(https://img\.shields\.io/badge/Unreal%20Engine-(?P<min>\d+\.\d+)--(?P<max>\d+\.\d+)-"),
    ),
    VersionRangeCheck(
        "README_en.md",
        "body",
        re.compile(r"\*\*Unreal Engine\s+(?P<min>\d+\.\d+)\s*~\s*(?P<max>\d+\.\d+)\*\*"),
    ),
    VersionRangeCheck(
        "Fab_StoreDescription.md",
        "summary",
        re.compile(r"対応バージョン\s+UE(?P<min>\d+\.\d+)-(?P<max>\d+\.\d+)"),
    ),
    VersionRangeCheck(
        "Fab_StoreDescription.md",
        "supported engine versions",
        re.compile(r"Supported Engine Versions:\s+Unreal Engine\s+(?P<min>\d+\.\d+)\s*-\s*(?P<max>\d+\.\d+)"),
    ),
    VersionRangeCheck(
        "Fab_StoreDescription.txt",
        "supported engine versions",
        re.compile(r"Supported Engine Versions:\s+Unreal Engine\s+(?P<min>\d+\.\d+)\s*-\s*(?P<max>\d+\.\d+)"),
    ),
]


def read_text(repo_relative_path: str) -> str:
    return (REPO_ROOT / repo_relative_path).read_text(encoding="utf-8")


def read_check_file(repo_relative_path: str, failures: list[str], skipped: list[str], skipped_paths: set[str]) -> str | None:
    path = REPO_ROOT / repo_relative_path
    if path.exists():
        return path.read_text(encoding="utf-8")

    if repo_relative_path in MANDATORY_FILES:
        failures.append(f"{repo_relative_path}: expected file, found file not found")
    elif repo_relative_path not in skipped_paths:
        skipped.append(f"skipped (file not found): {repo_relative_path}")
        skipped_paths.add(repo_relative_path)

    return None


def format_range(min_version: str, max_version: str) -> str:
    return f"{min_version}~{max_version}"


def check_version_ranges(skipped: list[str], skipped_paths: set[str]) -> tuple[list[str], int]:
    # 7箇所のUE対応バージョン範囲を抽出し、存在しない任意ファイルはスキップします。
    failures: list[str] = []
    found_ranges: list[tuple[VersionRangeCheck, str]] = []

    for check in VERSION_RANGE_CHECKS:
        text = read_check_file(check.path, failures, skipped, skipped_paths)
        if text is None:
            continue

        match = check.pattern.search(text)
        if match is None:
            failures.append(f"{check.path} {check.label}: version range pattern not found")
            continue
        found_ranges.append((check, format_range(match.group("min"), match.group("max"))))

    range_counts = Counter(actual_range for _, actual_range in found_ranges)
    expected_range = range_counts.most_common(1)[0][0] if found_ranges else "version range pattern"
    for check, actual_range in found_ranges:
        if actual_range != expected_range:
            failures.append(f"{check.path} {check.label}: expected {expected_range}, found {actual_range}")

    return failures, len(found_ranges)


def check_changelog_version(skipped: list[str], skipped_paths: set[str]) -> tuple[list[str], str | None]:
    # upluginのVersionNameが更新履歴の見出し行に存在するか確認し、未追跡のchangelogはスキップします。
    uplugin_path = "Plugins/KawaiiPhysics/KawaiiPhysics.uplugin"
    changelog_path = "Fab_Changelog.txt"
    failures: list[str] = []

    plugin_text = read_check_file(uplugin_path, failures, skipped, skipped_paths)
    if plugin_text is None:
        return failures, None

    plugin = json.loads(plugin_text)
    expected_version = plugin.get("VersionName")
    if not isinstance(expected_version, str) or not expected_version:
        return [f"{uplugin_path} VersionName: expected non-empty string, found {expected_version!r}"], None

    changelog_text = read_check_file(changelog_path, failures, skipped, skipped_paths)
    if changelog_text is None:
        return failures, None

    heading_lines = [line.strip() for line in changelog_text.splitlines()]
    if expected_version not in heading_lines:
        failures.append(f"{changelog_path} heading: version heading not found for {expected_version}")

    return failures, expected_version


def main() -> int:
    failures = []
    skipped: list[str] = []
    skipped_paths: set[str] = set()
    range_failures, checked_range_count = check_version_ranges(skipped, skipped_paths)
    changelog_failures, changelog_version = check_changelog_version(skipped, skipped_paths)
    failures.extend(range_failures)
    failures.extend(changelog_failures)

    for skip in skipped:
        print(skip)

    if failures:
        for failure in failures:
            print(failure)
        return 1

    changelog_summary = f" and changelog VersionName {changelog_version} was found" if changelog_version else ""
    print(f"Version consistency check passed: {checked_range_count} UE ranges are aligned{changelog_summary}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
