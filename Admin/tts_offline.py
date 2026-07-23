#!/usr/bin/env python3
"""
txt_to_mp3.py - Converte un file di testo (.txt) in un file audio MP3,
completamente offline.

Il formato consigliato e' .txt: e' testo puro, senza simboli di formattazione,
quindi la lettura risulta pulita. I file .md vengono comunque accettati:
lo script rimuove automaticamente la sintassi Markdown piu' comune
(#, *, `, link, ecc.) prima della sintesi vocale.

Funzionamento:
  1. pyttsx3 genera un file WAV temporaneo usando il motore vocale
     del sistema operativo (nessuna connessione internet richiesta)
  2. pydub converte il WAV in MP3 tramite ffmpeg
  3. L'MP3 viene salvato nella stessa cartella del file di input,
     con lo stesso nome (es. documento.txt -> documento.mp3)

Installazione delle dipendenze:
    pip install pyttsx3 pydub

Serve inoltre ffmpeg per la conversione in MP3:
    Windows: winget install ffmpeg   (oppure scaricarlo da ffmpeg.org)
    macOS:   brew install ffmpeg
    Linux:   sudo apt install ffmpeg espeak-ng

Esempi d'uso:
    python txt_to_mp3.py documento.txt
    python txt_to_mp3.py appunti.md
    python txt_to_mp3.py documento.txt --voice italian --rate 160
    python txt_to_mp3.py --list-voices
"""

import argparse
import re
import sys
import tempfile
from pathlib import Path

try:
    import pyttsx3
except ImportError:
    sys.exit("Errore: pyttsx3 non installato. Esegui: pip install pyttsx3")

try:
    from pydub import AudioSegment
except ImportError:
    sys.exit("Errore: pydub non installato. Esegui: pip install pydub")


def strip_markdown(text):
    """Rimuove la sintassi Markdown piu' comune per una lettura pulita."""
    # Blocchi di codice
    text = re.sub(r"```.*?```", "", text, flags=re.DOTALL)
    # Codice inline
    text = re.sub(r"`([^`]*)`", r"\1", text)
    # Immagini: ![alt](url) -> alt
    text = re.sub(r"!\[([^\]]*)\]\([^)]*\)", r"\1", text)
    # Link: [testo](url) -> testo
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text)
    # Titoli: rimuove i cancelletti iniziali
    text = re.sub(r"^#{1,6}\s*", "", text, flags=re.MULTILINE)
    # Grassetto e corsivo
    text = re.sub(r"(\*{1,3}|_{1,3})(.+?)\1", r"\2", text)
    # Elenchi puntati e numerati
    text = re.sub(r"^\s*[-*+]\s+", "", text, flags=re.MULTILINE)
    text = re.sub(r"^\s*\d+\.\s+", "", text, flags=re.MULTILINE)
    # Citazioni
    text = re.sub(r"^>\s*", "", text, flags=re.MULTILINE)
    # Linee orizzontali
    text = re.sub(r"^([-*_])\1{2,}\s*$", "", text, flags=re.MULTILINE)
    # Tabelle: rimuove le barre verticali
    text = text.replace("|", " ")
    # Righe vuote multiple
    text = re.sub(r"\n{3,}", "\n\n", text)
    return text.strip()


def list_voices(engine):
    """Stampa tutte le voci disponibili nel sistema."""
    for i, voice in enumerate(engine.getProperty("voices")):
        print(f"[{i}] {voice.name}  (id: {voice.id})")


def select_voice(engine, query):
    """Seleziona una voce per indice, id o parte del nome."""
    voices = engine.getProperty("voices")
    if query.isdigit():
        idx = int(query)
        if 0 <= idx < len(voices):
            engine.setProperty("voice", voices[idx].id)
            return voices[idx]
        sys.exit(f"Errore: indice voce {idx} non valido (0-{len(voices) - 1}).")
    q = query.lower()
    for voice in voices:
        if q in voice.id.lower() or q in voice.name.lower():
            engine.setProperty("voice", voice.id)
            return voice
    sys.exit(f"Errore: nessuna voce corrisponde a '{query}'. "
             "Usa --list-voices per vedere quelle disponibili.")


def main():
    parser = argparse.ArgumentParser(
        description="Converte un file .txt o .md in un MP3, offline.")
    parser.add_argument("input", nargs="?",
                        help="File di testo da convertire (.txt o .md)")
    parser.add_argument("--voice",
                        help="Voce da usare: indice, id o parte del nome")
    parser.add_argument("--rate", type=int, default=175,
                        help="Velocita' di lettura in parole/minuto "
                             "(default: 175)")
    parser.add_argument("--bitrate", default="128k",
                        help="Bitrate dell'MP3 (default: 128k)")
    parser.add_argument("--list-voices", action="store_true",
                        help="Elenca le voci disponibili ed esce")
    args = parser.parse_args()

    engine = pyttsx3.init()

    if args.list_voices:
        list_voices(engine)
        return

    if not args.input:
        parser.error("Specificare il file di input (.txt o .md).")

    input_path = Path(args.input)
    if not input_path.is_file():
        sys.exit(f"Errore: il file '{input_path}' non esiste.")
    if input_path.suffix.lower() not in (".txt", ".md"):
        sys.exit("Errore: sono supportati solo file .txt e .md.")

    try:
        text = input_path.read_text(encoding="utf-8").strip()
    except UnicodeDecodeError:
        # Fallback per file non UTF-8 (es. salvati da Windows)
        text = input_path.read_text(encoding="latin-1").strip()

    if input_path.suffix.lower() == ".md":
        text = strip_markdown(text)

    if not text:
        sys.exit("Errore: il file e' vuoto.")

    engine.setProperty("rate", args.rate)
    if args.voice:
        chosen = select_voice(engine, args.voice)
        print(f"Voce selezionata: {chosen.name}")

    # L'MP3 viene salvato nella stessa cartella del file di input
    output_path = input_path.with_suffix(".mp3")

    print(f"Sintesi vocale in corso ({len(text)} caratteri)...")
    with tempfile.TemporaryDirectory() as tmpdir:
        wav_path = Path(tmpdir) / "temp.wav"
        engine.save_to_file(text, str(wav_path))
        engine.runAndWait()

        if not wav_path.is_file() or wav_path.stat().st_size == 0:
            sys.exit("Errore: la sintesi vocale non ha prodotto audio. "
                     "Verifica che il motore vocale del sistema funzioni.")

        print("Conversione in MP3...")
        try:
            audio = AudioSegment.from_wav(str(wav_path))
            audio.export(str(output_path), format="mp3",
                         bitrate=args.bitrate)
        except FileNotFoundError:
            sys.exit("Errore: ffmpeg non trovato. Installalo e assicurati "
                     "che sia nel PATH (vedi istruzioni in cima al file).")

    duration = round(audio.duration_seconds)
    print(f"Fatto: {output_path}  ({duration // 60} min {duration % 60} s)")


if __name__ == "__main__":
    main()