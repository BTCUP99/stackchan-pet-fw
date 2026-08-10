import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class VibeIntegrationGuardsTest(unittest.TestCase):
    def test_streetpass_resumes_outside_active_vibe_mode(self) -> None:
        generator = (ROOT / "integrations" / "vibe" / "prepare_sources.py").read_text(
            encoding="utf-8"
        )

        self.assertIn(
            '"  if (VibeMode::active() && VibeMode::bleLinkConnected()) {\\n"',
            generator,
        )
        self.assertNotIn(
            '"  if (VibeMode::bleLinkConnected()) {\\n"\n'
            "    f'    return \"{vibe_mode_name}_hid\";\\n'",
            generator,
        )


if __name__ == "__main__":
    unittest.main()
