import { useId, useRef, useState } from 'react'
import { ChevronDown } from 'lucide-react'
import {
  crsPresetMetaLine,
  findCrsPreset,
  groupCrsPresets,
  searchCrsPresets,
  type CommonCrsPreset,
} from './commonCrsPresets'

interface CrsPickerProps {
  // The existing horizontalCrs form value; the picker never owns canonical
  // CRS state — it writes exactly the chosen identifier (or manual text).
  value: string
  onChange: (code: string) => void
}

interface PickerOption {
  key: string
  preset: CommonCrsPreset | null
}

// Searchable CRS combobox over the curated common-CRS list. Purely a UX
// layer on top of the existing horizontalCrs string: selecting an entry
// writes exactly its identifier, manual/free-text entry stays available, and
// no value is ever converted or substituted. The engine's Geo service
// remains the validation authority; geographic presets are selectable and
// visibly marked invalid so users can discover why they fail.
//
// Dismissal is a component-level focus boundary: when focus leaves the
// picker root (Tab, Shift+Tab, click into another field or empty dialog
// space where relatedTarget is null/outside), the list closes. Options and
// the toggle suppress mousedown's focus shift, so their clicks fire before
// any blur-based dismissal can interfere.
export function CrsPicker({ value, onChange }: CrsPickerProps) {
  const uid = useId()
  const inputRef = useRef<HTMLInputElement>(null)
  const [open, setOpen] = useState(false)
  const [query, setQuery] = useState('')
  const [activeIndex, setActiveIndex] = useState(0)

  const results = searchCrsPresets(open ? query : '')
  const groups = groupCrsPresets(results)
  const options: PickerOption[] = [
    ...results.map((preset) => ({ key: preset.code, preset: preset as CommonCrsPreset })),
    { key: 'manual', preset: null },
  ]
  const activeOption = options[activeIndex] ?? null
  const selectedPreset = findCrsPreset(value)

  const optionId = (key: string) => `${uid}-opt-${key.replace(/[^a-zA-Z0-9-]/g, '-')}`

  const openDropdown = () => {
    setQuery('')
    setActiveIndex(0)
    setOpen(true)
  }

  const closeDropdown = (refocus = false) => {
    setOpen(false)
    setQuery('')
    if (refocus) {
      inputRef.current?.focus()
    }
  }

  const choose = (option: PickerOption) => {
    if (option.preset) {
      onChange(option.preset.code)
    }
    // Manual entry keeps the typed text as-is; the field value is already
    // the raw input, so nothing is rewritten or converted here.
    closeDropdown(true)
  }

  const handleKeyDown = (event: React.KeyboardEvent<HTMLInputElement>) => {
    if (event.key === 'ArrowDown') {
      event.preventDefault()
      if (!open) {
        openDropdown()
        return
      }
      setActiveIndex((index) => Math.min(index + 1, options.length - 1))
    } else if (event.key === 'ArrowUp') {
      event.preventDefault()
      if (!open) {
        openDropdown()
        return
      }
      setActiveIndex((index) => Math.max(index - 1, 0))
    } else if (event.key === 'Home' && open) {
      event.preventDefault()
      setActiveIndex(0)
    } else if (event.key === 'End' && open) {
      event.preventDefault()
      setActiveIndex(options.length - 1)
    } else if (event.key === 'Enter' && open) {
      // Select the highlighted entry instead of submitting the form.
      event.preventDefault()
      if (activeOption) {
        choose(activeOption)
      }
    } else if (event.key === 'Escape' && open) {
      event.preventDefault()
      closeDropdown()
    }
  }

  return (
    <div
      className="crs-picker"
      onBlur={(event) => {
        // Focus boundary: close when focus moves outside the picker (Tab,
        // Shift+Tab, click into another control, or a click on
        // non-focusable space where relatedTarget is null). Focus moving
        // within the picker (input -> option is prevented via mousedown)
        // does not close.
        if (!event.currentTarget.contains(event.relatedTarget as Node | null)) {
          closeDropdown()
        }
      }}
    >
      <div className="crs-field">
        <input
          ref={inputRef}
          className="form-input crs-input"
          role="combobox"
          aria-expanded={open}
          aria-controls={open ? `${uid}-listbox` : undefined}
          aria-activedescendant={open && activeOption ? optionId(activeOption.key) : undefined}
          aria-autocomplete="list"
          aria-label="Horizontal CRS"
          autoComplete="off"
          value={value}
          placeholder="EPSG:32633"
          onChange={(event) => {
            onChange(event.target.value)
            if (open) {
              setQuery(event.target.value)
              setActiveIndex(0)
            }
          }}
          onKeyDown={handleKeyDown}
          onFocus={() => {
            if (!open) {
              openDropdown()
            }
          }}
        />
        <button
          className="crs-toggle"
          type="button"
          tabIndex={-1}
          aria-label={open ? 'Close CRS list' : 'Open CRS list'}
          onMouseDown={(event) => event.preventDefault()}
          onClick={() => (open ? closeDropdown(true) : openDropdown())}
        >
          <ChevronDown size={14} />
        </button>
        {open ? (
          <div className="crs-listbox" role="listbox" aria-label="Common CRS presets" id={`${uid}-listbox`}>
            {groups.map((group) => (
              <div key={group.category} className="crs-group" role="presentation">
                <div className="crs-group-label" role="presentation">
                  {group.label}
                </div>
                {group.items.map((preset) => {
                  const index = options.findIndex((option) => option.key === preset.code)
                  return (
                    <div
                      key={preset.code}
                      id={optionId(preset.code)}
                      role="option"
                      aria-selected={value === preset.code}
                      className={
                        'crs-option' + (index === activeIndex ? ' active' : '') + (value === preset.code ? ' selected' : '')
                      }
                      onMouseDown={(event) => event.preventDefault()}
                      onClick={() => choose({ key: preset.code, preset })}
                      onMouseEnter={() => setActiveIndex(index)}
                    >
                      <span className="crs-option-code">
                        {preset.code} ({preset.name})
                      </span>
                      <span className={'crs-option-meta' + (preset.projectCrsAllowed ? '' : ' crs-option-invalid')}>
                        {crsPresetMetaLine(preset)}
                      </span>
                    </div>
                  )
                })}
              </div>
            ))}
            {results.length === 0 ? (
              <div className="crs-empty" role="presentation">
                No common CRS matches “{query}”. Enter any CRS code manually.
              </div>
            ) : null}
            <div
              id={optionId('manual')}
              role="option"
              aria-selected={false}
              className={'crs-option crs-option-manual' + (activeIndex === options.length - 1 ? ' active' : '')}
              onMouseDown={(event) => event.preventDefault()}
              onClick={() => closeDropdown(true)}
              onMouseEnter={() => setActiveIndex(options.length - 1)}
            >
              <span className="crs-option-code">Enter CRS manually…</span>
              <span className="crs-option-meta">Type any CRS identifier, e.g. EPSG:25832</span>
            </div>
          </div>
        ) : null}
      </div>
      {selectedPreset ? (
        <p
          className={
            'crs-caption' + (selectedPreset.projectCrsAllowed ? '' : ' crs-caption-invalid')
          }
        >
          {selectedPreset.name} — {crsPresetMetaLine(selectedPreset)}
        </p>
      ) : null}
    </div>
  )
}
