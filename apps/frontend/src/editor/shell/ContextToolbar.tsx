import { ContextToolShelf, type ContextToolShelfProps } from './ContextToolShelf'

// Backwards-compatible ContextToolbar forwarding to ContextToolShelf
export function ContextToolbar(props: ContextToolShelfProps) {
  return <ContextToolShelf {...props} />
}

export { ContextToolShelf }
