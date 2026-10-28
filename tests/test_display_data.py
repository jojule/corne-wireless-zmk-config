"""Checks for misleading legends, inherited actions and stale generated data."""

import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("display_data", ROOT / "scripts/generate-display-data.py")
DATA = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = DATA
SPEC.loader.exec_module(DATA)


class DisplayDataTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (ROOT / "config/corne.keymap").read_text()
        cls.bindings = DATA.parse_layers(cls.source)

    def test_all_physical_bindings_have_fitting_legends(self):
        self.assertEqual([len(layer) for layer in self.bindings], [42] * 4)
        self.assertEqual(len(set(DATA.PHYSICAL[0] + DATA.PHYSICAL[1])), 36)
        for layer in self.bindings:
            for position in DATA.PHYSICAL[0] + DATA.PHYSICAL[1]:
                DATA.legend_for_binding(layer[position])  # constructor enforces pixel budget

    def test_named_profiles_keep_original_slots_and_positions(self):
        expected = {14: ('16', 'MBP 16"'), 15: ('PH', 'iPhone'), 16: ('IP', 'iPad Pro'),
                    27: ('14', 'MBP 14"'), 28: ('VP', 'Vision Pro')}
        for position, (short, full) in expected.items():
            legend = DATA.legend_for_binding(self.bindings[3][position])
            self.assertEqual(legend.short, short)
            self.assertEqual(legend.lines, ('SELECT', full))
        clear = DATA.legend_for_binding(self.bindings[3][26])
        self.assertEqual(clear.lines, ('CLEAR THIS', 'BT PAIRING'))

    def test_shift_key_and_distinct_mouse_actions(self):
        z = [DATA.legend_for_binding(layer[25]) for layer in self.bindings]
        self.assertEqual([entry.short for entry in z], ['Z', 'SH', '.', 'Bv'])
        self.assertEqual(z[0].lines, ('TAP Z', 'HOLD SHIFT'))
        self.assertEqual(z[2].lines, ('TAP .', 'HOLD SHIFT'))
        cursor = DATA.legend_for_binding(self.bindings[1][20])
        pointer = DATA.legend_for_binding(self.bindings[1][8])
        scroll = DATA.legend_for_binding(self.bindings[1][6])
        self.assertEqual(cursor.lines, ('DOWN ARROW',))
        self.assertEqual(pointer.lines, ('POINTER', 'DOWN'))
        self.assertEqual(scroll.lines, ('SCROLL', 'DOWN'))
        self.assertEqual(DATA.legend_for_binding(self.bindings[2][20]).lines, ('KEYPAD', 'MULTIPLY'))

    def test_macros_preserve_case_and_meaning(self):
        DATA.validate_behavior_definitions(self.source)
        self.assertEqual(DATA.legend_for_binding('ae').short, 'a:')
        self.assertEqual(DATA.legend_for_binding('cae').short, 'A:')
        broken = self.source.replace('&macro_tap &kp A', '&macro_tap &kp X', 1)
        with self.assertRaisesRegex(ValueError, 'Macro ae changed'):
            DATA.generate(broken)

    def test_unknown_behavior_or_keycode_is_not_silently_labelled(self):
        with self.assertRaisesRegex(ValueError, 'Unknown binding'):
            DATA.legend_for_binding('bt BT_CLR_ALL')
        with self.assertRaisesRegex(ValueError, 'Unknown keycode'):
            DATA.legend_for_binding('kp MAGIC')

    def test_known_edit_invalidates_generated_data(self):
        current = DATA.generate(self.source)
        changed = DATA.generate(self.source.replace('&ht GLOBE Q', '&ht GLOBE Y', 1))
        self.assertNotEqual(current, changed)
        self.assertEqual(current, (ROOT / 'src/utility/keymap_data.c').read_text())

    def test_compiled_resolution_uses_real_stack_and_stops_at_unassigned(self):
        harness = r'''
#include <assert.h>
#include <string.h>
#include <corne_utility/keymap_data.h>
int main(void) {
    /* System and Symbols inherit slash; an active Navigate below them supplies End. */
    assert(!strcmp(corne_legend_resolve(0x0e, 34)->short_label, "EN"));
    assert(!strcmp(corne_legend_lookup(3, 34)->short_label, "/"));
    assert(corne_legend_raw(3, 34)->flags & CORNE_LEGEND_TRANSPARENT);
    /* An explicit empty System E is not the Navigate Down arrow beneath it. */
    assert(corne_legend_resolve(0x0a, 20)->flags & CORNE_LEGEND_UNASSIGNED);
    assert(!strcmp(corne_legend_resolve(0, 25)->short_label, "Z"));
    assert(corne_legend_raw(255, 255)->flags & CORNE_LEGEND_UNASSIGNED);
    assert(corne_legend_lookup(255, 25)->flags & CORNE_LEGEND_UNASSIGNED);
    int count = 0;
    for (int i = 0; i < 256; ++i) count += corne_legend_is_physical(i);
    assert(count == 36);
    assert(!corne_legend_is_physical(0) && !corne_legend_is_physical(35));
    assert(corne_legend_is_physical(36) && corne_legend_is_physical(41));
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            (path / 'check.c').write_text(harness)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'include'),
                            str(ROOT / 'src/utility/keymap_data.c'), str(path / 'check.c'),
                            '-o', str(path / 'check')], check=True)
            subprocess.run([str(path / 'check')], check=True)


if __name__ == '__main__':
    unittest.main()
