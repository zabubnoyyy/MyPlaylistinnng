import os
import asyncio
from aiogram import Bot, Dispatcher, types
from aiogram.types import InputFile
from yt_dlp import YoutubeDL
from pydub import AudioSegment

API_TOKEN = '8364064673:AAG2zBIZ1kQswvmzmBtdLAH6ebkKPcJOUGQ'
MAX_FILE_SIZE = 2 * 1024 * 1024 * 1024  # 2 GB

bot = Bot(token=API_TOKEN)
dp = Dispatcher(bot)

async def download_tracks(playlist_url, download_dir='downloads'):
    os.makedirs(download_dir, exist_ok=True)
    ydl_opts = {
        'format': 'bestaudio/best',
        'outtmpl': f'{download_dir}/%(title)s.%(ext)s',
        'postprocessors': [{
            'key': 'FFmpegExtractAudio',
            'preferredcodec': 'mp3',
            'preferredquality': '192',
        }],
        'quiet': True,
        'ignoreerrors': True,
    }
    failed = []
    filenames = []
    with YoutubeDL(ydl_opts) as ydl:
        info_dict = ydl.extract_info(playlist_url, download=False)
        for entry in info_dict['entries']:
            try:
                ydl.download([entry['webpage_url']])
                name = ydl.prepare_filename(entry).replace('.webm', '.mp3').replace('.m4a', '.mp3')
                filenames.append(name)
            except Exception:
                failed.append({'title': entry.get('title'), 'url': entry.get('webpage_url')})
    return filenames, failed

def merge_mp3(files, output_file='merged.mp3'):
    combined = AudioSegment.empty()
    for file in files:
        audio = AudioSegment.from_mp3(file)
        combined += audio
    combined.export(output_file, format="mp3")
    return output_file

def split_file(input_file, chunk_size=MAX_FILE_SIZE):
    files = []
    audio = AudioSegment.from_mp3(input_file)
    total_length = len(audio)
    start = 0
    part = 1
    while start < total_length:
        end = start + int(chunk_size * 1000 / (audio.frame_rate * audio.frame_width))
        chunk = audio[start:end]
        chunk_name = f"{input_file}_part{part}.mp3"
        chunk.export(chunk_name, format="mp3")
        files.append(chunk_name)
        start = end
        part += 1
    return files

@dp.message_handler(commands=['start'])
async def send_welcome(message: types.Message):
    await message.reply("Вітаю! Надішліть посилання на плейлист YouTube Music.")

@dp.message_handler()
async def handle_playlist(message: types.Message):
    playlist_url = message.text.strip()
    await message.reply("Завантажую плейлист, будь ласка, зачекайте...")
    files, failed = await asyncio.get_event_loop().run_in_executor(None, download_tracks, playlist_url)
    if failed:
        failed_titles = "\n".join([f"{t['title']} ({t['url']})" for t in failed])
        markup = types.ReplyKeyboardMarkup(resize_keyboard=True)
        markup.add("Пропустити", "Зупинити")
        await message.reply(f"Не вдалося завантажити:\n{failed_titles}\nЩо робити?", reply_markup=markup)
        return

    merged_file = merge_mp3(files, 'merged.mp3')
    file_size = os.path.getsize(merged_file)
    if file_size > MAX_FILE_SIZE:
        chunks = split_file(merged_file)
        for chunk in chunks:
            await message.answer_document(InputFile(chunk))
            os.remove(chunk)
    else:
        await message.answer_document(InputFile(merged_file))
        os.remove(merged_file)
    # Clean up
    for f in files:
        try:
            os.remove(f)
        except Exception:
            pass

@dp.message_handler(lambda message: message.text in ["Пропустити", "Зупинити"])
async def failed_track_action(message: types.Message):
    if message.text == "Зупинити":
        await message.reply("Завантаження зупинено.")
    else:
        await message.reply("Продовжую обробку без недоступних треків.")
        # Тут можна повторно викликати процес без недоступних треків, якщо потрібно.

if __name__ == "__main__":
    from aiogram import executor
    executor.start_polling(dp, skip_updates=True)