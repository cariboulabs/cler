<script lang="ts">
  import { BaseEdge, getBezierPath, type EdgeProps } from '@xyflow/svelte';
  import type { EdgePoint, RoutedEdge } from './project';

  const props: EdgeProps<RoutedEdge> = $props();

  const STUB = 14;

  function routedPoints(bends: EdgePoint[]): EdgePoint[] {
    const start = { x: props.sourceX, y: props.sourceY };
    const exit = { x: props.sourceX + STUB, y: props.sourceY };
    const entry = { x: props.targetX - STUB, y: props.targetY };
    const end = { x: props.targetX, y: props.targetY };
    const first = bends[0];
    const last = bends[bends.length - 1];
    if (!first || !last) return [start, exit, entry, end];
    return [
      start,
      exit,
      { x: exit.x, y: first.y },
      ...bends,
      { x: entry.x, y: last.y },
      entry,
      end
    ];
  }

  function polyline(points: EdgePoint[]): string {
    return points.map((point, i) => `${i === 0 ? 'M' : 'L'}${point.x},${point.y}`).join(' ');
  }

  const path = $derived.by(() => {
    const bends = props.data?.bends ?? [];
    if (bends.length === 0) {
      const [bezier] = getBezierPath({
        sourceX: props.sourceX,
        sourceY: props.sourceY,
        sourcePosition: props.sourcePosition,
        targetX: props.targetX,
        targetY: props.targetY,
        targetPosition: props.targetPosition
      });
      return bezier;
    }
    return polyline(routedPoints(bends));
  });
</script>

<BaseEdge id={props.id} {path} markerEnd={props.markerEnd} style={props.style} />
