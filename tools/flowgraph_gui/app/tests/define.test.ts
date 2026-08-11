import { describe, expect, it } from 'vitest';
import shipped from './palette.json';
import {
  defineCommand,
  defineGaps,
  definePreview,
  defineRefusal,
  emptyDefineForm,
  nameGap,
  paletteTypes,
  shapeMessage,
  structSpan,
  type DefineForm
} from '../src/lib/define';
import type { BlockSpec } from '../src/lib/palette';

const specs = shipped.blocks as unknown as BlockSpec[];
const PATH = '/tmp/fake/hello_world.cpp';

function form(patch: Partial<DefineForm> = {}): DefineForm {
  return { ...emptyDefineForm(), name: 'MyScaleBlock', ...patch };
}

function fields(gaps: { field: string | null }[]): (string | null)[] {
  return gaps.map((gap) => gap.field);
}

function messageOn(target: DefineForm, field: string): string | undefined {
  return defineGaps(target, specs, PATH).find((gap) => gap.field === field)?.message;
}

describe('the wizard refuses before the crate has to', () => {
  it('accepts a well-formed block and reports no gaps', () => {
    expect(defineGaps(form(), specs, PATH)).toEqual([]);
  });

  it('demands the Block suffix and a real identifier', () => {
    expect(nameGap(form({ name: '' }), specs, PATH)?.message).toBe('required');
    expect(nameGap(form({ name: 'MyScale' }), specs, PATH)?.message).toBe(
      'a block type name must end in "Block"'
    );
    expect(nameGap(form({ name: 'Block' }), specs, PATH)?.message).toBe(
      'a block type name must end in "Block"'
    );
    expect(nameGap(form({ name: 'My Scale Block' }), specs, PATH)?.message).toBe(
      '"My Scale Block" is not a valid C++ identifier'
    );
    expect(nameGap(form({ name: 'MyScaleBlock' }), specs, PATH)).toBe(null);
  });

  it('pre-checks the name against palette specs the crate cannot see', () => {
    expect(nameGap(form({ name: 'SinkFileBlock' }), specs, PATH)).toEqual({
      field: 'name',
      message: 'SinkFileBlock already exists in sinks'
    });
    const local: BlockSpec = { ...specs[0]!, name: 'PlantBlock', origin: PATH };
    expect(nameGap(form({ name: 'PlantBlock' }), [local], PATH)?.message).toBe(
      'PlantBlock already exists in this file'
    );
  });

  it('blocks the 0-in 0-out shape with the message the crate uses', () => {
    const flat = form({ inputs: [], outputs: 0 });
    expect(defineGaps(flat, specs, PATH)).toEqual([
      { field: 'outputs', message: shapeMessage('MyScaleBlock') }
    ]);
    expect(shapeMessage('MyScaleBlock')).toBe('MyScaleBlock declares no inputs and no outputs');
    expect(defineGaps(form({ inputs: [], outputs: 1 }), specs, PATH)).toEqual([]);
    expect(defineGaps(form({ inputs: [{ name: 'in' }], outputs: 0 }), specs, PATH)).toEqual([]);
  });

  it('refuses the names the generated scaffold already owns', () => {
    expect(messageOn(form({ inputs: [{ name: 'name' }] }), 'input.0')).toBe(
      '"name" is the generated display-name parameter'
    );
    expect(messageOn(form({ inputs: [{ name: 'out0' }] }), 'input.0')).toBe(
      '"out0" is a generated output parameter'
    );
    expect(messageOn(form({ inputs: [{ name: 'in' }, { name: 'in' }] }), 'input.1')).toBe(
      '"in" is already used by another port or parameter'
    );
    expect(messageOn(form({ inputs: [{ name: '_in' }] }), 'input.0')).toBe(
      '"_in" becomes a reserved identifier as a member'
    );
    expect(messageOn(form({ inputs: [{ name: '2in' }] }), 'input.0')).toBe(
      '"2in" is not a valid C++ identifier'
    );
    expect(messageOn(form({ outputs: 2, inputs: [{ name: 'out1' }] }), 'input.0')).toBe(
      '"out1" is a generated output parameter'
    );
    expect(defineGaps(form({ outputs: 1, inputs: [{ name: 'out1' }] }), specs, PATH)).toEqual([]);
  });

  it('holds parameters to the same namespace and demands a type', () => {
    const shadow = form({ params: [{ name: 'in', cppType: 'float', default: '' }] });
    expect(messageOn(shadow, 'param.0.name')).toBe(
      '"in" is already used by another port or parameter'
    );
    const untyped = form({ params: [{ name: 'gain', cppType: '  ', default: '' }] });
    expect(messageOn(untyped, 'param.0.type')).toBe('required');
    const underscored = form({ params: [{ name: '_gain', cppType: 'float', default: '' }] });
    expect(messageOn(underscored, 'param.0.name')).toBe(
      '"_gain" becomes a reserved identifier as a member'
    );
  });

  it('demands a value type', () => {
    expect(fields(defineGaps(form({ valueType: '' }), specs, PATH))).toEqual(['value_type']);
  });
});

describe('the wizard describes what it will generate', () => {
  it('summarises the ports and the constructor without rendering C++', () => {
    const preview = definePreview(
      form({
        inputs: [{ name: 'in' }],
        outputs: 2,
        params: [
          { name: 'gain', cppType: 'float', default: '1.0f' },
          { name: 'taps', cppType: 'size_t', default: '' }
        ],
        mayBlock: true
      })
    );
    expect(preview.ports).toBe('in → out0, out1 · float');
    expect(preview.ctor).toBe('MyScaleBlock(const char* name, float gain = 1.0f, size_t taps)');
    expect(preview.note).toContain('may_block');
    expect(preview.ctor).not.toContain('cler::Channel');
  });

  it('names the empty shapes rather than showing a blank', () => {
    const preview = definePreview(form({ name: '', inputs: [], outputs: 0, valueType: '' }));
    expect(preview.ports).toBe('none → none · T');
    expect(preview.ctor).toBe('NewBlock(const char* name)');
  });

  it('offers the concrete element types the palette already carries', () => {
    const types = paletteTypes(specs);
    expect(types).toContain('float');
    expect(types).toContain('std::complex<float>');
    expect(types).not.toContain('T');
    expect([...types]).toEqual([...types].sort((a, b) => a.localeCompare(b, 'en')));
  });
});

describe('the wizard sends one define_block', () => {
  it('trims every field and nulls an empty default', () => {
    expect(
      defineCommand(2, {
        name: '  MyScaleBlock ',
        valueType: ' float ',
        inputs: [{ name: ' in ' }],
        outputs: 1,
        params: [
          { name: ' gain ', cppType: ' float ', default: ' 2.0f ' },
          { name: 'taps', cppType: 'size_t', default: '   ' }
        ],
        mayBlock: true
      })
    ).toEqual({
      command: 'define_block',
      site: 2,
      name: 'MyScaleBlock',
      value_type: 'float',
      inputs: [{ name: 'in' }],
      outputs: 1,
      params: [
        { name: 'gain', cpp_type: 'float', default: '2.0f' },
        { name: 'taps', cpp_type: 'size_t', default: null }
      ],
      may_block: true
    });
  });
});

describe('every crate refusal lands on the field that caused it', () => {
  const target = form({
    inputs: [{ name: 'left' }, { name: 'right' }],
    params: [
      { name: 'gain', cppType: 'not a type', default: '' },
      { name: 'taps', cppType: 'size_t', default: '1.0f); std::abort(; (0' }
    ]
  });

  it('routes the whole matrix', () => {
    expect(defineRefusal({ error: 'invalid_block_name', name: 'Gain' }, target)).toEqual({
      field: 'name',
      message: 'a block type name must end in "Block"'
    });
    expect(defineRefusal({ error: 'duplicate_type', name: 'AddBlock' }, target)).toEqual({
      field: 'name',
      message: 'AddBlock is already a type in this file'
    });
    expect(
      defineRefusal({ error: 'invalid_type', element: 'value_type', text: '1.0f' }, target)
    ).toEqual({ field: 'value_type', message: '"1.0f" is not a valid C++ type' });
    expect(
      defineRefusal({ error: 'invalid_type', element: 'cpp_type', text: 'not a type' }, target)
    ).toEqual({ field: 'param.0.type', message: '"not a type" is not a valid C++ type' });
    expect(
      defineRefusal(
        { error: 'invalid_expression', element: 'default', text: '1.0f); std::abort(; (0' },
        target
      )?.field
    ).toBe('param.1.default');
    expect(defineRefusal({ error: 'duplicate_variable', var_name: 'right' }, target)).toEqual({
      field: 'input.1',
      message: '"right" is already used by another port or parameter'
    });
    expect(defineRefusal({ error: 'duplicate_variable', var_name: 'name' }, target)).toEqual({
      field: null,
      message: '"name" is the generated display-name parameter'
    });
    expect(defineRefusal({ error: 'reserved_identifier', text: 'gain' }, target)).toEqual({
      field: 'param.0.name',
      message: '"gain" is a C++ keyword'
    });
    expect(defineRefusal({ error: 'reserved_identifier', text: '_gain' }, target)?.message).toBe(
      '"_gain" becomes a reserved identifier as a member'
    );
    expect(defineRefusal({ error: 'invalid_identifier', text: 'left' }, target)?.field).toBe(
      'input.0'
    );
    expect(defineRefusal({ error: 'invalid_identifier', text: 'My Block' }, target)?.field).toBe(
      'name'
    );
    expect(
      defineRefusal(
        { error: 'unsupported_shape', detail: 'MyScaleBlock declares no inputs and no outputs' },
        target
      )
    ).toEqual({ field: 'outputs', message: shapeMessage('MyScaleBlock') });
    expect(defineRefusal({ error: 'revision_mismatch' }, target)).toBe(null);
    expect(defineRefusal(null, target)).toBe(null);
  });
});

describe('the code drawer can find the struct that was just written', () => {
  it('spans the struct header and misses what is not there', () => {
    const source = 'int main() {}\n\nstruct MyScaleBlock : public cler::BlockBase {\n};\n';
    expect(structSpan(source, 'MyScaleBlock')).toEqual({ start: 15, end: 34 });
    expect(source.slice(15, 34)).toBe('struct MyScaleBlock');
    expect(structSpan(source, 'OtherBlock')).toBe(null);
  });
});
