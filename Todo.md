# To do list

[ ] Сделать "Глубокое" нажатие на кнопки поворота антенны

/
├── AGENTS.md
├── README.md
├── CONTEXT.md              # временно оставить, чтобы не ломать CMake
├── ARCHITECTURE.md         # временно оставить, чтобы не ломать CMake
│
└── docs/
    ├── README.md
    │
    ├── spec/
    │   ├── scope.md        # из CONTEXT.md, но скорректировать
    │   └── glossary.md     # новый файл
    │
    ├── architecture/
    │   ├── layers.md       # из ARCHITECTURE.md, но скорректировать
    │   └── data-flow.md    # новый короткий файл
    │
    └── development/
        └── build-and-test.md
		
		
		Да, в целом понятно. Документация уже задает хорошую рамку: текущий scope, запреты для QML, слои, поток данных,
  хранение, симулятор, тесты и приоритеты источников описаны достаточно ясно, чтобы по ним работать.

  Нашел несколько мест, которые лучше поправить до активной разработки:

  1. Карта документации ссылается на файлы, которых нет.
     В /C:/Qt/no_vector_projects/SiriusScope/docs/README.md:28 и /C:/Qt/no_vector_projects/SiriusScope/AGENTS.md:32
     перечислены docs/domain/*, docs/ui/*, docs/hardware/*, docs/storage/*, но этих файлов сейчас нет. Для Codex это
     будет выглядеть как обязательные документы, которые невозможно прочитать. Лучше либо создать короткие stub-файлы с
     TBD, либо пометить в docs/README.md, что они запланированы, но еще не существуют.
  2. Есть небольшое архитектурное расхождение по слоям.
     В /C:/Qt/no_vector_projects/SiriusScope/AGENTS.md:62 перечислены 5 слоев без отдельного Processing Layer, а /C:/Qt/
     no_vector_projects/SiriusScope/docs/architecture/layers.md:30 вводит отдельный Processing Layer. Это разумный слой
     для агрегации/пеленгации, но его надо добавить и в AGENTS.md, иначе инструкции конфликтуют.
  3. ТЗ местами смешивает hardware/storage в Infrastructure.
     В /C:/Qt/no_vector_projects/SiriusScope/docs/spec/SiriusScope_TZ_v0.1.md:133 аппаратные адаптеры и storage
     перечислены внутри Infrastructure layer, а отдельные документы уже разделяют Infrastructure Layer и Hardware
     Adapter Layer. Я бы считал новую архитектурную документацию более точной, но в ТЗ стоит явно написать, что это
     раннее укрупненное разделение или обновить схему.
  4. Стек разработки не до конца согласован.
     /C:/Qt/no_vector_projects/SiriusScope/docs/spec/scope.md:79 и build guide говорят C++20 / Qt 6 / QML / CMake, а ТЗ
     добавляет обязательные Boost и Conan в /C:/Qt/no_vector_projects/SiriusScope/docs/spec/SiriusScope_TZ_v0.1.md:183.
     В текущем CMakeLists.txt Boost/Conan не используются. Нужно решить: они обязательны, допустимы при необходимости,
     или это устаревший пункт.
  5. Markdown-косметика: несколько блоков закрыты четырьмя backtick вместо трех.
     Например /C:/Qt/no_vector_projects/SiriusScope/docs/spec/scope.md:55, /C:/Qt/no_vector_projects/SiriusScope/docs/
     architecture/layers.md:39, /C:/Qt/no_vector_projects/SiriusScope/docs/development/build-and-test.md:24. Это обычно
     рендерится нормально, но выглядит как опечатка и лучше унифицировать.

  Главное: требования понятны. Самое важное перед дальнейшей работой — закрыть отсутствующие документы или явно
  обозначить их как planned, и синхронизировать Processing Layer / Boost / Conan между AGENTS.md, scope и ТЗ.