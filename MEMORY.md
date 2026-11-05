# Project memory / Память проекта

## Permanent rule: no forced speed-to-gear bindings

Never force a particular numbered gear at a fixed road speed. Do not add speed-to-gear tables or fixed gear targets for kickdown/cruise. Speeds, RPM and gears mentioned in user messages, screenshots and logs are diagnostic reproduction examples, not control requirements. Use native gearbox decisions and current game data. A failed-upshift ceiling may be learned from an observed failed shift; it must not be a fixed target gear. This rule persists across sessions and applies to all supported cars.

## Постоянное правило: никаких принудительных привязок скорости к передаче

Нельзя принудительно включать определённую передачу при фиксированном числе скорости. Не добавлять таблицы «скорость → передача» и фиксированные цели передач для кикдауна или круиза. Скорости, обороты и передачи из сообщений владельца, скриншотов и логов — примеры для диагностики, а не требования к управлению. Использовать штатные решения коробки и текущие игровые данные. Временный предел повторных повышений можно выводить из наблюдаемого неудачного переключения, но не задавать фиксированной целевой передачей. Правило сохраняется между сессиями и действует для всех поддерживаемых автомобилей.

See / См.: [AGENTS.md](AGENTS.md), [CLAUDE.md](CLAUDE.md), [engineering notes](docs/DEVELOPMENT.md).

## Economy cruise behavior / Поведение круиза Economy

Economy keeps its manual accelerator behavior. Active cruise uses the current plugin Normal recovery path for any speed deficit, not only a target increase; the existing Normal hooks and guard remain active. At or above the target it returns to Economy. The game setting is never written.

Economy сохраняет поведение ручного газа. При любом отставании активного круиза от цели используется разгон Normal из текущей сборки плагина, с уже действующими перехватами и защитой. Это относится и к увеличению цели, и к последующей потере скорости. При достижении/превышении цели возвращается Economy. Настройку игры не менять.
