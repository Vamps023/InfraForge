import type { LucideIcon } from 'lucide-react'
import {
  MousePointer2,
  Slash,
  Circle,
  TrendingUp,
  PenLine,
} from 'lucide-react'

export type AuthoringToolId =
  | 'select'
  | 'road.straight'
  | 'road.arc'
  | 'road.clothoid'
  | 'road.polyline'
  | 'road.move-control'
  | 'road.insert-control'

export interface AuthoringToolDef {
  id: AuthoringToolId
  label: string
  shortcut: string
  icon: LucideIcon
  description: string
  category: 'navigation' | 'road-design'
  statusHint: string
  minPoints?: number
  maxPoints?: number
}

export const AUTHORING_TOOLS: Record<AuthoringToolId, AuthoringToolDef> = {
  select: {
    id: 'select',
    label: 'Select',
    shortcut: 'V',
    icon: MousePointer2,
    description: 'Select alignment, control points, or inspection targets',
    category: 'navigation',
    statusHint: 'Click road in viewport to select. Drag or click control points to inspect.',
  },
  'road.straight': {
    id: 'road.straight',
    label: 'Straight',
    shortcut: 'S',
    icon: Slash,
    description: 'Create a canonical straight line segment directly between two endpoints',
    category: 'road-design',
    statusHint: 'Click start point, then click end point.',
    minPoints: 2,
    maxPoints: 2,
  },
  'road.arc': {
    id: 'road.arc',
    label: 'Circle Arc',
    shortcut: 'A',
    icon: Circle,
    description: 'Create a canonical circular arc passing through 3 points (Start, Curve, End)',
    category: 'road-design',
    statusHint: 'Click 3 points: start point, arc pass-through point, and end point.',
    minPoints: 3,
    maxPoints: 3,
  },
  'road.clothoid': {
    id: 'road.clothoid',
    label: 'Clothoid Arc',
    shortcut: 'C',
    icon: TrendingUp,
    description: 'Create a canonical transition spiral with continuous curvature',
    category: 'road-design',
    statusHint: 'Click start point, then click direction to set tangent heading and length.',
    minPoints: 1,
    maxPoints: 2,
  },
  'road.polyline': {
    id: 'road.polyline',
    label: 'Polyline',
    shortcut: 'P',
    icon: PenLine,
    description: 'Draw multi-point alignment polyline fitted deterministically by the native engine',
    category: 'road-design',
    statusHint: 'Click to place control points. Press Enter to finish or Esc to cancel.',
    minPoints: 2,
  },
  'road.move-control': {
    id: 'road.move-control',
    label: 'Move Control',
    shortcut: 'M',
    icon: MousePointer2,
    description: 'Move selected control point and refit',
    category: 'road-design',
    statusHint: 'Click new position for selected control point.',
  },
  'road.insert-control': {
    id: 'road.insert-control',
    label: 'Insert Control',
    shortcut: 'I',
    icon: MousePointer2,
    description: 'Insert new control point into alignment polyline and refit',
    category: 'road-design',
    statusHint: 'Click position to insert new control point.',
  },
}
