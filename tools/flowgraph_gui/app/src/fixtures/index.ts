import type { ParseResult } from '../lib/schema';
import adsb_receiver from './adsb_receiver.json';
import hello_world from './hello_world.json';
import mass_spring_damper from './mass_spring_damper.json';
import plots from './plots.json';
import polyphase_channelizer from './polyphase_channelizer.json';
import spike from './spike.json';
import type_conflict from './type_conflict.json';
import uhd_device from './uhd_device.json';
import adsb_receiver_source from '../../../../../desktop_examples/adsb_receiver.cpp?raw';
import hello_world_source from '../../../../../desktop_examples/hello_world.cpp?raw';
import mass_spring_damper_source from '../../../../../desktop_examples/mass_spring_damper.cpp?raw';
import plots_source from '../../../../../desktop_examples/plots.cpp?raw';
import polyphase_channelizer_source from '../../../../../desktop_examples/polyphase_channelizer.cpp?raw';
import spike_source from '../../../../../desktop_examples/spike/spike.cpp?raw';
import type_conflict_source from '../../../cler-graph/tests/data/type_conflict.cpp?raw';
import uhd_device_source from '../../../../../desktop_examples/uhd_device.cpp?raw';

export const fixtures: Record<string, ParseResult> = {
  hello_world,
  plots,
  polyphase_channelizer,
  mass_spring_damper,
  uhd_device,
  spike,
  adsb_receiver,
  type_conflict
} as unknown as Record<string, ParseResult>;

export const fixtureSources: Record<string, string> = {
  hello_world: hello_world_source,
  plots: plots_source,
  polyphase_channelizer: polyphase_channelizer_source,
  mass_spring_damper: mass_spring_damper_source,
  uhd_device: uhd_device_source,
  spike: spike_source,
  adsb_receiver: adsb_receiver_source,
  type_conflict: type_conflict_source
};

export const fixtureNames = Object.keys(fixtures);
