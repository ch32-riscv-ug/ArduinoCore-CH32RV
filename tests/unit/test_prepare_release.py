"""tools/index/prepare_release.py: the release workflow moves CHANGELOG.md's Unreleased items under the version and
puts that section in the GitHub Release's body."""
import importlib.util
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
_spec = importlib.util.spec_from_file_location("prepare_release", ROOT / "tools/index/prepare_release.py")
prepare_release = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(prepare_release)

LOG = ["# Changelog", "", "## Unreleased", "- (EN) a", "- (JA) あ", "", "## 0.8.1", "- (EN) old", "- (JA) 古い"]


def test_moves_unreleased_under_the_version():
    lines = prepare_release.update_changelog(LOG, "0.9.0")
    assert lines == ["# Changelog", "", "## Unreleased", "", "## 0.9.0", "- (EN) a", "- (JA) あ", "",
                     "## 0.8.1", "- (EN) old", "- (JA) 古い"]
    assert prepare_release.release_body(lines, "0.9.0") == "- (EN) a\n- (JA) あ\n"


def test_a_rerun_leaves_the_file_alone():
    once = prepare_release.update_changelog(LOG, "0.9.0")
    assert prepare_release.update_changelog(once, "0.9.0") == once


def test_nothing_unreleased_says_so():
    lines = prepare_release.update_changelog(["# Changelog", "", "## Unreleased"], "0.9.0")
    assert prepare_release.release_body(lines, "0.9.0").startswith("- (EN) No user-facing changes recorded.")


def test_the_committed_changelog_has_unreleased():
    lines = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8").splitlines()
    assert "## Unreleased" in lines
