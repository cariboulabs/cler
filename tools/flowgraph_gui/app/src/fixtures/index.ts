import type { ParseResult } from '../lib/schema';
import adsb_receiver from './adsb_receiver.json';
import hello_world from './hello_world.json';
import mass_spring_damper from './mass_spring_damper.json';
import plots from './plots.json';
import polyphase_channelizer from './polyphase_channelizer.json';
import spike from './spike.json';
import type_conflict from './type_conflict.json';
import uhd_device from './uhd_device.json';

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

export const fixtureNames = Object.keys(fixtures);
