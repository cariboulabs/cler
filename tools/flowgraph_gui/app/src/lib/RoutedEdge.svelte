<script lang="ts">
  import { BaseEdge, getBezierPath, type EdgeProps } from '@xyflow/svelte';
  import type { EdgePoint, RoutedEdge } from './project';

  const props: EdgeProps<RoutedEdge> = $props();

  const STUB = 14;

  type Segment = { from: EdgePoint; to: EdgePoint; length: number };

  function segments(points: EdgePoint[]): Segment[] {
    const [first, ...rest] = points;
    if (!first) return [];
    let previous = first;
    const parts: Segment[] = [];
    for (const point of rest) {
      parts.push({
        from: previous,
        to: point,
        length: Math.hypot(point.x - previous.x, point.y - previous.y)
      });
      previous = point;
    }
    return parts;
  }

  function midpoint(points: EdgePoint[]): EdgePoint {
    const parts = segments(points);
    const half = parts.reduce((total, part) => total + part.length, 0) / 2;
    let walked = 0;
    for (const part of parts) {
      if (walked + part.length >= half) {
        const t = part.length === 0 ? 0 : (half - walked) / part.length;
        return {
          x: part.from.x + (part.to.x - part.from.x) * t,
          y: part.from.y + (part.to.y - part.from.y) * t
        };
      }
      walked += part.length;
    }
    return { x: props.targetX, y: props.targetY };
  }

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

  const geometry = $derived.by(() => {
    const bends = props.data?.bends ?? [];
    if (bends.length === 0) {
      const [path, labelX, labelY] = getBezierPath({
        sourceX: props.sourceX,
        sourceY: props.sourceY,
        sourcePosition: props.sourcePosition,
        targetX: props.targetX,
        targetY: props.targetY,
        targetPosition: props.targetPosition
      });
      return { path, labelX, labelY };
    }
    const points = routedPoints(bends);
    const label = midpoint(points);
    return { path: polyline(points), labelX: label.x, labelY: label.y };
  });
</script>

<BaseEdge
  id={props.id}
  path={geometry.path}
  labelX={geometry.labelX}
  labelY={geometry.labelY}
  label={props.label}
  markerEnd={props.markerEnd}
  style={props.style}
/>
