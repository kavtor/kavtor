#!/usr/bin/env python3
"""Generate a casparMIX config with native metering and synchronized channels."""
import argparse
import xml.etree.ElementTree as ET
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path, help='New file; requires the patched CasparCG audio-metering path')
    args = parser.parse_args()
    if args.input.resolve() == args.output.resolve():
        parser.error('Use a separate output file to preserve the stock-server configuration')
    tree = ET.parse(args.input)
    removed = 0
    for consumers in tree.findall('./channels/channel/consumers'):
        for consumer in list(consumers):
            fixtures = consumer.find('fixtures')
            if consumer.tag == 'artnet' and fixtures is not None and len(fixtures) == 0:
                consumers.remove(consumer)
                removed += 1
    for channel in tree.findall('./channels/channel'):
        group = channel.find('sync-group')
        if group is None:
            group = ET.SubElement(channel, 'sync-group')
        group.text = 'kavtor'
    ET.indent(tree, space='    ')
    tree.write(args.output, encoding='utf-8', xml_declaration=True)
    print(f'Removed {removed} empty Art-Net consumers; casparMIX 0.3.0 required; channels share the kavtor frame clock')


if __name__ == '__main__':
    main()
