/*
 * Minimal Windows launcher for locally packaged Worldbuilding preview builds.
 *
 * It locates the MSYS2 UCRT64 runtime, prepares the runtime data/plugin paths,
 * starts the companion GPlates executable, and exits. Deliberately do not
 * enumerate or force-show child windows: doing so can resurrect GPlates' hidden
 * Qt splash screen as a second blank window.
 */

#include <windows.h>
#include <shellapi.h>

#include <string>
#include <vector>


namespace
{
	const wchar_t *const APPLICATION_NAME = L"GPlates Worldbuilding Preview";
	const wchar_t *const COMPANION_NAME = L"gplates-worldbuilding-qol-app.exe";

	bool
	file_exists(
			const std::wstring &path)
	{
		const DWORD attributes = GetFileAttributesW(path.c_str());
		return attributes != INVALID_FILE_ATTRIBUTES &&
				!(attributes & FILE_ATTRIBUTE_DIRECTORY);
	}

	bool
	runtime_is_valid(
			const std::wstring &runtime_bin)
	{
		return file_exists(runtime_bin + L"\\Qt6Core.dll") &&
				file_exists(runtime_bin + L"\\libgdal-39.dll");
	}

	std::wstring
	get_environment_variable(
			const wchar_t *name)
	{
		const DWORD required_size = GetEnvironmentVariableW(name, NULL, 0);
		if (!required_size)
		{
			return std::wstring();
		}
		std::vector<wchar_t> buffer(required_size);
		GetEnvironmentVariableW(name, &buffer[0], required_size);
		return std::wstring(&buffer[0]);
	}

	std::wstring
	launcher_directory()
	{
		std::vector<wchar_t> buffer(32768);
		const DWORD length = GetModuleFileNameW(NULL, &buffer[0],
				static_cast<DWORD>(buffer.size()));
		if (!length || length >= buffer.size())
		{
			return std::wstring();
		}
		const std::wstring path(&buffer[0], length);
		const std::wstring::size_type separator = path.find_last_of(L"\\/");
		return separator == std::wstring::npos
				? std::wstring() : path.substr(0, separator);
	}

	std::wstring
	find_runtime_bin()
	{
		std::vector<std::wstring> candidates;
		const std::wstring configured = get_environment_variable(L"GPLATES_UCRT64_BIN");
		if (!configured.empty())
		{
			candidates.push_back(configured);
		}
		const std::wstring local_app_data = get_environment_variable(L"LOCALAPPDATA");
		if (!local_app_data.empty())
		{
			candidates.push_back(local_app_data + L"\\msys64\\ucrt64\\bin");
		}
		candidates.push_back(L"C:\\msys64\\ucrt64\\bin");
		candidates.push_back(L"E:\\msys64\\ucrt64\\bin");

		for (std::vector<std::wstring>::const_iterator iter = candidates.begin();
				iter != candidates.end(); ++iter)
		{
			if (runtime_is_valid(*iter))
			{
				return *iter;
			}
		}
		return std::wstring();
	}

	std::wstring
	quote_argument(
			const std::wstring &argument)
	{
		std::wstring result(L"\"");
		unsigned int backslashes = 0;
		for (std::wstring::const_iterator iter = argument.begin(); iter != argument.end(); ++iter)
		{
			if (*iter == L'\\')
			{
				++backslashes;
				continue;
			}
			if (*iter == L'\"')
			{
				result.append(backslashes * 2 + 1, L'\\');
				result.push_back(L'\"');
				backslashes = 0;
				continue;
			}
			result.append(backslashes, L'\\');
			backslashes = 0;
			result.push_back(*iter);
		}
		result.append(backslashes * 2, L'\\');
		result.push_back(L'\"');
		return result;
	}

	void
	prepend_to_path(
			const std::wstring &directory)
	{
		const std::wstring old_path = get_environment_variable(L"PATH");
		SetEnvironmentVariableW(L"PATH",
				(old_path.empty() ? directory : directory + L";" + old_path).c_str());
	}
}


int WINAPI
wWinMain(
		HINSTANCE,
		HINSTANCE,
		PWSTR,
		int)
{
	const std::wstring directory = launcher_directory();
	if (directory.empty())
	{
		MessageBoxW(NULL, L"Could not determine the preview launcher path.",
				APPLICATION_NAME, MB_OK | MB_ICONERROR);
		return 1;
	}

	const std::wstring companion = directory + L"\\" + COMPANION_NAME;
	if (!file_exists(companion))
	{
		MessageBoxW(NULL, L"The preview application is missing beside this launcher.",
				APPLICATION_NAME, MB_OK | MB_ICONERROR);
		return 1;
	}

	const std::wstring runtime_bin = find_runtime_bin();
	if (runtime_bin.empty())
	{
		MessageBoxW(NULL,
				L"The UCRT64 runtime was not found. Set GPLATES_UCRT64_BIN to the folder "
				L"containing Qt6Core.dll and libgdal-39.dll.",
				APPLICATION_NAME, MB_OK | MB_ICONERROR);
		return 1;
	}

	prepend_to_path(runtime_bin);
	SetEnvironmentVariableW(L"QT_PLUGIN_PATH", (runtime_bin + L"\\share\\qt6\\plugins").c_str());
	SetEnvironmentVariableW(L"GDAL_DATA", (runtime_bin + L"\\share\\gdal").c_str());
	SetEnvironmentVariableW(L"PROJ_DATA", (runtime_bin + L"\\share\\proj").c_str());

	std::wstring command_line = quote_argument(companion) + L" --no-python";
	int argument_count = 0;
	LPWSTR *arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
	if (arguments)
	{
		for (int index = 1; index < argument_count; ++index)
		{
			command_line += L" " + quote_argument(arguments[index]);
		}
		LocalFree(arguments);
	}

	std::vector<wchar_t> writable_command_line(command_line.begin(), command_line.end());
	writable_command_line.push_back(L'\0');
	STARTUPINFOW startup_info = {};
	startup_info.cb = sizeof(startup_info);
	PROCESS_INFORMATION process_info = {};
	if (!CreateProcessW(companion.c_str(), &writable_command_line[0], NULL, NULL, FALSE,
			0, NULL, directory.c_str(), &startup_info, &process_info))
	{
		MessageBoxW(NULL, L"Windows could not start the GPlates preview application.",
				APPLICATION_NAME, MB_OK | MB_ICONERROR);
		return 1;
	}

	CloseHandle(process_info.hThread);
	CloseHandle(process_info.hProcess);
	return 0;
}
