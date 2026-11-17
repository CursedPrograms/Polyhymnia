# Polyhymnia: writes lang/<code>.txt from this table. Run: python make_packs.py
import os

CODES = ['ja', 'zh', 'es', 'ko', 'fr', 'de', 'ru', 'sr', 'af', 'yua', 'pdc']
T = {
    'File': ['ファイル', '文件', 'Archivo', '파일', 'Fichier', 'Datei', 'Файл', 'Датотека', 'Lêer', "Ju'un", 'Feil'],
    'Help': ['ヘルプ', '帮助', 'Ayuda', '도움말', 'Aide', 'Hilfe', 'Справка', 'Помоћ', 'Hulp', "Áantaj", 'Hilf'],
    'Quit': ['終了', '退出', 'Salir', '종료', 'Quitter', 'Beenden', 'Выход', 'Излаз', 'Verlaat', "Xu'ulsik", 'Uffheere'],
    'New': ['新規', '新建', 'Nuevo', '새로 만들기', 'Nouveau', 'Neu', 'Создать', 'Ново', 'Nuut', 'Túumben', 'Nei'],
    'Open...': ['開く...', '打开...', 'Abrir...', '열기...', 'Ouvrir...', 'Öffnen...', 'Открыть...', 'Отвори...', 'Open...', "Je'ik...", 'Uffmache...'],
    'Save': ['保存', '保存', 'Guardar', '저장', 'Enregistrer', 'Speichern', 'Сохранить', 'Сачувај', 'Stoor', "Cha'paj", 'Speichere'],
    'Preferences...': ['環境設定...', '首选项...', 'Preferencias...', '환경 설정...', 'Préférences...', 'Einstellungen...', 'Настройки...', 'Подешавања...', 'Voorkeure...', "Ba'ax tz'ite'...", 'Iwwerschtellunge...'],
    'About Polyhymnia': ['Polyhymnia について', '关于 Polyhymnia', 'Acerca de Polyhymnia', 'Polyhymnia 정보', 'À propos de Polyhymnia', 'Über Polyhymnia', 'О Polyhymnia', 'О програму Polyhymnia', 'Oor Polyhymnia', "Yo'olal Polyhymnia", 'Iwwer Polyhymnia'],
    'SESSION': ['セッション', '工程', 'SESIÓN', '세션', 'SESSION', 'SESSION', 'СЕССИЯ', 'СЕСИЈА', 'SESSIE', 'SESIÓN', 'SITZUNG'],
    'No project open yet.': ['プロジェクトはまだ開いていません。', '尚未打开工程。', 'Aún no hay proyecto abierto.', '아직 연 프로젝트가 없습니다.', 'Aucun projet ouvert.', 'Noch kein Projekt geöffnet.', 'Проект ещё не открыт.', 'Још нема отвореног пројекта.', 'Nog geen projek oop nie.', 'Minaan jeelmej.', 'Noch ken Projekt off.'],
    'File > New to begin.': ['開始するには [ファイル] > [新規]。', '用 文件 > 新建 开始。', 'Archivo > Nuevo para empezar.', '시작하려면 파일 > 새로 만들기.', 'Fichier > Nouveau pour commencer.', 'Datei > Neu zum Starten.', 'Файл > Создать, чтобы начать.', 'Датотека > Ново за почетак.', 'Lêer > Nuut om te begin.', 'Archivo > Túumben.', 'Feil > Neu zum Aanfange.'],
    'AUDIO': ['オーディオ', '音频', 'AUDIO', '오디오', 'AUDIO', 'AUDIO', 'АУДИО', 'АУДИО', 'OUDIO', 'AUDIO', 'AUDIO'],
    'Audio device open': ['オーディオデバイス接続', '音频设备已打开', 'Dispositivo de audio abierto', '오디오 장치 열림', 'Périphérique audio ouvert', 'Audiogerät geöffnet', 'Аудиоустройство открыто', 'Аудио уређај отворен', 'Oudiotoestel oop', 'Dispositivo audio', 'Audiogerät off'],
    'Could not open the audio device': ['オーディオデバイスを開けません', '无法打开音频设备', 'No se pudo abrir el dispositivo de audio', '오디오 장치를 열 수 없음', 'Périphérique audio impossible', 'Audiogerät konnte nicht geöffnet werden', 'Не удалось открыть аудиоустройство', 'Не могу да отворим аудио уређај', 'Kon nie oudiotoestel oopmaak nie', 'Ma tu paajtal u jeik', 'Konnt Audio nit uffmache'],
    'Edit window': ['編集ウィンドウ', '编辑窗口', 'Ventana de edición', '편집 창', 'Fenêtre édition', 'Bearbeitungsfenster', 'Окно редактирования', 'Прозор за уређивање', 'Wysigingsvenster', 'Ventana edición', 'Bearbeitungsfenster'],
    'Tracks and the timeline arrive in the next phases.': ['トラックとタイムラインは次の段階で追加されます。', '音轨和时间线将在后续阶段加入。', 'Las pistas y la línea de tiempo llegan en las siguientes fases.', '트랙과 타임라인은 다음 단계에서 옵니다.', 'Les pistes arrivent ensuite.', 'Spuren und Zeitleiste kommen in den nächsten Phasen.', 'Дорожки появятся в следующих фазах.', 'Траке стижу у следећим фазама.', 'Snitte kom in volgende fases.', 'U tsáaloob ku taal.', 'Gleise kumme später.'],
    'Preferences': ['環境設定', '首选项', 'Preferencias', '환경 설정', 'Préférences', 'Einstellungen', 'Настройки', 'Подешавања', 'Voorkeure', "Ba'ax tz'ite'", 'Iwwerschtellunge'],
    'General': ['一般', '常规', 'General', '일반', 'Général', 'Allgemein', 'Общие', 'Опште', 'Algemeen', 'General', 'Allgemein'],
    'Language': ['言語', '语言', 'Idioma', '언어', 'Langue', 'Sprache', 'Язык', 'Језик', 'Taal', "T'aan", 'Schprooch'],
    'Audio': ['オーディオ', '音频', 'Audio', '오디오', 'Audio', 'Audio', 'Аудио', 'Аудио', 'Oudio', 'Audio', 'Audio'],
    'Playback device': ['再生デバイス', '播放设备', 'Dispositivo de reproducción', '재생 장치', 'Périphérique de lecture', 'Wiedergabegerät', 'Устройство воспроизведения', 'Уређај за репродукцију', 'Speeltoestel', 'Dispositivo reproducción', 'Wiedergabegerät'],
    '(default)': ['(既定)', '(默认)', '(predeterminado)', '(기본값)', '(par défaut)', '(Standard)', '(по умолчанию)', '(подразумевано)', '(verstek)', '(tuux suuk)', '(Schtandard)'],
    'Buffer size (frames)': ['バッファサイズ (フレーム)', '缓冲区大小 (帧)', 'Tamaño del búfer (fotogramas)', '버퍼 크기(프레임)', 'Taille du tampon (trames)', 'Puffergröße (Frames)', 'Размер буфера (кадры)', 'Величина бафера (оквири)', 'Buffergrootte (raamwerk)', 'Búfer (frames)', 'Puffergrössi (Frames)'],
    'Apply': ['適用', '应用', 'Aplicar', '적용', 'Appliquer', 'Anwenden', 'Применить', 'Примени', 'Pas toe', 'Aplicar', 'Anwende'],
}

here = os.path.dirname(os.path.abspath(__file__))
for i, code in enumerate(CODES):
    with open(os.path.join(here, code + '.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('# Polyhymnia - %s (make_packs.py)\n' % code)
        for en, tr in T.items():
            f.write('%s = %s\n' % (en, tr[i]))
print('wrote', len(CODES), 'packs')