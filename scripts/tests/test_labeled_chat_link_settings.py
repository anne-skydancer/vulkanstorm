from pathlib import Path
import unittest
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[2]
SETTINGS = ROOT / 'indra/newview/app_settings/settings.xml'


def setting_fields(settings, target_name):
    entries = settings.find('map')
    for key, value in zip(entries[::2], entries[1::2]):
        if key.text == target_name:
            return {field.text: data.text for field, data in zip(value[::2], value[1::2])}
    raise AssertionError(f'Missing setting: {target_name}')


class LabeledChatLinkSettingsTest(unittest.TestCase):
    def test_real_destination_preview_is_default_and_user_setting_is_persisted(self):
        settings = ET.parse(SETTINGS).getroot()
        fields = setting_fields(settings, 'FSDisableLabeledChatLinks')

        self.assertEqual(fields['Type'], 'Boolean')
        self.assertEqual(fields['Value'], '1')
        self.assertEqual(fields['Persist'], '1')


if __name__ == '__main__':
    unittest.main()
