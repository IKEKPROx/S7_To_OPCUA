import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/material.dart';

void main() {
  runApp(const GatewayConsoleApp());
}

class GatewayConsoleApp extends StatelessWidget {
  const GatewayConsoleApp({super.key});

  @override
  Widget build(BuildContext context) {
    const accent = Color(0xFF0E7C7B);
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      title: 'S7ToOPCUA',
      theme: ThemeData(
        useMaterial3: true,
        colorScheme: ColorScheme.fromSeed(
          seedColor: accent,
          brightness: Brightness.light,
        ),
        scaffoldBackgroundColor: const Color(0xFFF6F7F5),
        inputDecorationTheme: const InputDecorationTheme(
          border: OutlineInputBorder(),
          isDense: true,
        ),
        textButtonTheme: TextButtonThemeData(
          style: TextButton.styleFrom(foregroundColor: accent),
        ),
        filledButtonTheme: FilledButtonThemeData(
          style: FilledButton.styleFrom(
            backgroundColor: accent,
            foregroundColor: Colors.white,
          ),
        ),
      ),
      home: const GatewayHomePage(),
    );
  }
}

class GatewayHomePage extends StatefulWidget {
  const GatewayHomePage({super.key});

  @override
  State<GatewayHomePage> createState() => _GatewayHomePageState();
}

class _GatewayHomePageState extends State<GatewayHomePage> {
  static const _types = [
    'BOOL',
    'BYTE',
    'SINT',
    'USINT',
    'INT',
    'UINT',
    'WORD',
    'DINT',
    'UDINT',
    'DWORD',
    'REAL',
    'LREAL',
  ];
  static const _areas = ['DB', 'M', 'I', 'Q'];

  final _opcuaPort = TextEditingController(text: '4840');
  final _plcName = TextEditingController(text: 'FakeLine');
  final _plcIp = TextEditingController(text: '127.0.0.1');
  final _plcPort = TextEditingController(text: '1102');
  final _rack = TextEditingController(text: '0');
  final _slot = TextEditingController(text: '1');
  final _pollMs = TextEditingController(text: '500');
  final _logScroll = ScrollController();

  late final TextEditingController _gatewayRoot;
  final List<TagDraft> _tags = [];
  final List<String> _logs = [];
  final Map<String, LiveValue> _liveValues = {};

  Process? _gatewayProcess;
  Process? _fakePlcProcess;
  Process? _monitorProcess;
  String? _lastConfigPath;
  Timer? _monitorStartTimer;

  bool get _gatewayRunning => _gatewayProcess != null;
  bool get _fakePlcRunning => _fakePlcProcess != null;
  bool get _monitorRunning => _monitorProcess != null;

  @override
  void initState() {
    super.initState();
    _gatewayRoot = TextEditingController(text: _findGatewayRoot());
    _loadFakePreset();
    _appendLog('App ready. Gateway root: ${_gatewayRoot.text}');
  }

  @override
  void dispose() {
    _gatewayProcess?.kill(ProcessSignal.sigint);
    _fakePlcProcess?.kill(ProcessSignal.sigint);
    _monitorProcess?.kill(ProcessSignal.sigint);
    _monitorStartTimer?.cancel();
    _opcuaPort.dispose();
    _plcName.dispose();
    _plcIp.dispose();
    _plcPort.dispose();
    _rack.dispose();
    _slot.dispose();
    _pollMs.dispose();
    _gatewayRoot.dispose();
    _logScroll.dispose();
    for (final tag in _tags) {
      tag.dispose();
    }
    super.dispose();
  }

  void _loadFakePreset() {
    _opcuaPort.text = '4840';
    _plcName.text = 'FakeLine';
    _plcIp.text = '127.0.0.1';
    _plcPort.text = '1102';
    _rack.text = '0';
    _slot.text = '1';
    _pollMs.text = '500';

    for (final tag in _tags) {
      tag.dispose();
    }
    _tags
      ..clear()
      ..addAll([
        TagDraft(initialName: 'Motor1_Speed', initialStart: '0', type: 'REAL'),
        TagDraft(
          initialName: 'Motor1_Current',
          initialStart: '4',
          type: 'REAL',
        ),
        TagDraft(
          initialName: 'Motor1_Running',
          initialStart: '8',
          initialBit: '0',
          type: 'BOOL',
        ),
        TagDraft(
          initialName: 'Motor1_Fault',
          initialStart: '8',
          initialBit: '1',
          type: 'BOOL',
        ),
        TagDraft(
          initialName: 'Heater_On',
          initialStart: '8',
          initialBit: '2',
          type: 'BOOL',
        ),
        TagDraft(initialName: 'Temperature', initialStart: '10', type: 'INT'),
        TagDraft(initialName: 'PartCounter', initialStart: '12', type: 'DINT'),
        TagDraft(initialName: 'StatusWord', initialStart: '16', type: 'WORD'),
        TagDraft(
          initialName: 'TotalRuntime',
          initialStart: '18',
          type: 'DWORD',
        ),
        TagDraft(initialName: 'Pressure', initialStart: '22', type: 'LREAL'),
        TagDraft(initialName: 'TankLevel', initialStart: '30', type: 'REAL'),
      ]);
  }

  Future<void> _startFakePlc() async {
    if (_fakePlcRunning) return;
    final root = _gatewayRoot.text.trim();
    final fakePlcPath = _join(root, 'fake_plc');
    if (!File(fakePlcPath).existsSync()) {
      _showMessage('找不到 fake_plc：$fakePlcPath');
      return;
    }

    try {
      final process = await Process.start(fakePlcPath, [
        _plcPort.text.trim(),
      ], workingDirectory: root);
      _fakePlcProcess = process;
      setState(() {});
      _appendLog('Started fake_plc on port ${_plcPort.text.trim()}.');
      _pipeProcess(process, 'fake_plc');
      unawaited(
        process.exitCode.then((code) {
          if (_fakePlcProcess == process && mounted) {
            setState(() => _fakePlcProcess = null);
            _appendLog('fake_plc exited with code $code.');
          }
        }),
      );
    } catch (error) {
      _appendLog('Failed to start fake_plc: $error');
      _showMessage('启动假 PLC 失败');
    }
  }

  Future<void> _stopFakePlc() async {
    final process = _fakePlcProcess;
    if (process == null) return;
    process.kill(ProcessSignal.sigint);
    _appendLog('Stopping fake_plc...');
  }

  Future<void> _writeConfigOnly() async {
    try {
      final path = await _writeRuntimeConfig();
      _showMessage('配置已生成');
      _appendLog('Wrote config: $path');
    } catch (error) {
      _showMessage(error.toString());
    }
  }

  Future<void> _startGateway() async {
    if (_gatewayRunning) return;
    try {
      final configPath = await _writeRuntimeConfig();
      final root = _gatewayRoot.text.trim();
      final gatewayPath = _join(root, 'gateway');
      if (!File(gatewayPath).existsSync()) {
        _showMessage('找不到 gateway：$gatewayPath');
        return;
      }

      final process = await Process.start(gatewayPath, [
        configPath,
      ], workingDirectory: root);
      _gatewayProcess = process;
      setState(() {});
      _appendLog('Started gateway with $configPath.');
      _pipeProcess(process, 'gateway');
      _scheduleMonitorStart();
      unawaited(
        process.exitCode.then((code) {
          if (_gatewayProcess == process && mounted) {
            _stopMonitorProcess(log: false);
            setState(() => _gatewayProcess = null);
            _appendLog('gateway exited with code $code.');
          }
        }),
      );
    } catch (error) {
      _appendLog('Failed to start gateway: $error');
      _showMessage('启动网关失败');
    }
  }

  Future<void> _stopGateway() async {
    final process = _gatewayProcess;
    if (process == null) return;
    _stopMonitorProcess();
    process.kill(ProcessSignal.sigint);
    _appendLog('Stopping gateway...');
  }

  void _scheduleMonitorStart() {
    _monitorStartTimer?.cancel();
    _monitorStartTimer = Timer(const Duration(seconds: 2), () {
      if (mounted && _gatewayRunning && !_monitorRunning) {
        unawaited(_startMonitor());
      }
    });
  }

  Future<void> _startMonitor() async {
    if (_monitorRunning) return;
    String configPath;
    try {
      configPath = await _writeRuntimeConfig();
    } catch (error) {
      _showMessage(error.toString());
      return;
    }

    final root = _gatewayRoot.text.trim();
    final monitorPath = _join(root, 'ua_monitor');
    if (!File(monitorPath).existsSync()) {
      _showMessage('找不到 ua_monitor：$monitorPath');
      return;
    }

    try {
      final process = await Process.start(monitorPath, [
        '--json-lines',
        configPath,
      ], workingDirectory: root);
      _monitorProcess = process;
      setState(() {});
      _appendLog('Started live value monitor.');
      _pipeMonitorProcess(process);
      process.stderr
          .transform(utf8.decoder)
          .transform(const LineSplitter())
          .listen((line) => _appendLog('[monitor] $line'));
      unawaited(
        process.exitCode.then((code) {
          if (_monitorProcess == process && mounted) {
            setState(() => _monitorProcess = null);
            _appendLog('live value monitor exited with code $code.');
          }
        }),
      );
    } catch (error) {
      _appendLog('Failed to start live value monitor: $error');
      _showMessage('启动实时监控失败');
    }
  }

  void _stopMonitorProcess({bool log = true}) {
    _monitorStartTimer?.cancel();
    final process = _monitorProcess;
    if (process == null) return;
    process.kill(ProcessSignal.sigint);
    if (log) _appendLog('Stopping live value monitor...');
  }

  Future<String> _writeRuntimeConfig() async {
    final root = _gatewayRoot.text.trim();
    if (root.isEmpty) {
      throw '请填写项目目录';
    }
    final runtimeDir = Directory(_join(_appDataDir(), 'runtime'));
    if (!runtimeDir.existsSync()) {
      runtimeDir.createSync(recursive: true);
    }
    final configPath = _join(runtimeDir.path, 'app_config.json');
    final config = _buildConfig();
    const encoder = JsonEncoder.withIndent('  ');
    File(configPath).writeAsStringSync('${encoder.convert(config)}\n');
    _lastConfigPath = configPath;
    _seedLiveValues(config);
    return configPath;
  }

  String _appDataDir() {
    final home = Platform.environment['HOME'];
    if (home == null || home.isEmpty) {
      return _join(Directory.systemTemp.path, 'S7ToOPCUA');
    }
    return _join(
      _join(_join(home, 'Library'), 'Application Support'),
      'S7ToOPCUA',
    );
  }

  Map<String, dynamic> _buildConfig() {
    final tagJson = <Map<String, dynamic>>[];
    for (final tag in _tags) {
      if (tag.name.text.trim().isEmpty) continue;
      tagJson.add(tag.toJson());
    }
    if (tagJson.isEmpty) {
      throw '至少需要一个点位';
    }

    return {
      'opcua': {'port': _parseInt(_opcuaPort, 'OPC UA 端口')},
      'plcs': [
        {
          'name': _requiredText(_plcName, 'PLC 名称'),
          'ip': _requiredText(_plcIp, 'PLC IP'),
          'port': _parseInt(_plcPort, 'PLC 端口'),
          'rack': _parseInt(_rack, 'Rack'),
          'slot': _parseInt(_slot, 'Slot'),
          'poll_interval_ms': _parseInt(_pollMs, '轮询周期'),
          'tags': tagJson,
        },
      ],
    };
  }

  int _parseInt(TextEditingController controller, String label) {
    final value = int.tryParse(controller.text.trim());
    if (value == null) throw '$label 必须是整数';
    return value;
  }

  String _requiredText(TextEditingController controller, String label) {
    final value = controller.text.trim();
    if (value.isEmpty) throw '$label 不能为空';
    return value;
  }

  void _pipeProcess(Process process, String name) {
    process.stdout
        .transform(utf8.decoder)
        .transform(const LineSplitter())
        .listen((line) => _appendLog('[$name] $line'));
    process.stderr
        .transform(utf8.decoder)
        .transform(const LineSplitter())
        .listen((line) => _appendLog('[$name] $line'));
  }

  void _pipeMonitorProcess(Process process) {
    process.stdout
        .transform(utf8.decoder)
        .transform(const LineSplitter())
        .listen((line) {
          if (line.trim().isEmpty) return;
          try {
            final decoded = jsonDecode(line);
            if (decoded is! Map<String, dynamic>) {
              _appendLog('[monitor] $line');
              return;
            }
            final value = LiveValue.fromJson(decoded);
            if (!mounted) return;
            setState(() => _liveValues[value.node] = value);
          } catch (_) {
            _appendLog('[monitor] $line');
          }
        });
  }

  void _seedLiveValues(Map<String, dynamic> config) {
    final plcs = config['plcs'];
    if (plcs is! List) return;
    final next = <String, LiveValue>{};
    for (final plc in plcs) {
      if (plc is! Map<String, dynamic>) continue;
      final plcName = '${plc['name']}';
      final tags = plc['tags'];
      if (tags is! List) continue;
      for (final tag in tags) {
        if (tag is! Map<String, dynamic>) continue;
        final tagName = '${tag['name']}';
        final node = '$plcName.$tagName';
        next[node] =
            _liveValues[node] ??
            LiveValue(
              plc: plcName,
              tag: tagName,
              node: node,
              value: '-',
              quality: 'WAIT',
              ok: false,
              updatedAt: null,
            );
      }
    }
    if (mounted) {
      setState(() {
        _liveValues
          ..clear()
          ..addAll(next);
      });
    } else {
      _liveValues
        ..clear()
        ..addAll(next);
    }
  }

  void _appendLog(String message) {
    final timestamp = DateTime.now().toIso8601String().substring(11, 19);
    if (!mounted) return;
    setState(() {
      _logs.add('$timestamp  $message');
      if (_logs.length > 800) {
        _logs.removeRange(0, _logs.length - 800);
      }
    });
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (_logScroll.hasClients) {
        _logScroll.animateTo(
          _logScroll.position.maxScrollExtent,
          duration: const Duration(milliseconds: 180),
          curve: Curves.easeOut,
        );
      }
    });
  }

  void _showMessage(String message) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(content: Text(message), behavior: SnackBarBehavior.floating),
    );
  }

  String _opcuaUrl() {
    final host = _plcIp.text.trim() == '127.0.0.1' ? '127.0.0.1' : '你的Mac局域网IP';
    return 'opc.tcp://$host:${_opcuaPort.text.trim()}';
  }

  @override
  Widget build(BuildContext context) {
    final statusColor = _gatewayRunning
        ? const Color(0xFF168A4A)
        : const Color(0xFF8A2D2D);
    return Scaffold(
      body: SafeArea(
        child: Column(
          children: [
            _Header(
              gatewayRunning: _gatewayRunning,
              fakePlcRunning: _fakePlcRunning,
              statusColor: statusColor,
              opcuaUrl: _opcuaUrl(),
            ),
            Expanded(child: _buildWorkspace()),
          ],
        ),
      ),
    );
  }

  Widget _buildWorkspace() {
    final settings = _SettingsPane(
      gatewayRoot: _gatewayRoot,
      opcuaPort: _opcuaPort,
      plcName: _plcName,
      plcIp: _plcIp,
      plcPort: _plcPort,
      rack: _rack,
      slot: _slot,
      pollMs: _pollMs,
      onFakePreset: () {
        setState(_loadFakePreset);
        _appendLog('Loaded fake PLC preset.');
      },
    );

    return LayoutBuilder(
      builder: (context, constraints) {
        if (constraints.maxWidth < 1080) {
          return ListView(
            children: [
              settings,
              const Divider(height: 1),
              SizedBox(height: 430, child: _buildTagEditor()),
              const Divider(height: 1),
              SizedBox(height: 360, child: _buildLiveValuesPane()),
              const Divider(height: 1),
              SizedBox(height: 540, child: _buildControlPane()),
            ],
          );
        }

        return Row(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            SizedBox(width: 330, child: settings),
            const VerticalDivider(width: 1),
            Expanded(
              child: Column(
                children: [
                  Expanded(flex: 5, child: _buildTagEditor()),
                  const Divider(height: 1),
                  Expanded(flex: 4, child: _buildLiveValuesPane()),
                ],
              ),
            ),
            const VerticalDivider(width: 1),
            SizedBox(width: 390, child: _buildControlPane()),
          ],
        );
      },
    );
  }

  Widget _buildTagEditor() {
    return Padding(
      padding: const EdgeInsets.fromLTRB(20, 18, 20, 18),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              const Icon(Icons.table_rows_outlined, size: 21),
              const SizedBox(width: 8),
              const Text(
                '点表',
                style: TextStyle(fontSize: 18, fontWeight: FontWeight.w700),
              ),
              const Spacer(),
              IconButton(
                tooltip: '添加点位',
                onPressed: () => setState(() => _tags.add(TagDraft())),
                icon: const Icon(Icons.add),
              ),
            ],
          ),
          const SizedBox(height: 12),
          Expanded(
            child: DecoratedBox(
              decoration: BoxDecoration(
                color: Colors.white,
                border: Border.all(color: const Color(0xFFD9DEDA)),
                borderRadius: BorderRadius.circular(8),
              ),
              child: ClipRRect(
                borderRadius: BorderRadius.circular(8),
                child: SingleChildScrollView(
                  scrollDirection: Axis.horizontal,
                  child: SizedBox(
                    width: 900,
                    child: ListView.separated(
                      itemCount: _tags.length + 1,
                      separatorBuilder: (context, index) =>
                          const Divider(height: 1),
                      itemBuilder: (context, index) {
                        if (index == 0) return _TagHeader();
                        final tag = _tags[index - 1];
                        return _TagRow(
                          tag: tag,
                          areas: _areas,
                          types: _types,
                          onChanged: () => setState(() {}),
                          onDelete: () {
                            setState(() {
                              _tags.removeAt(index - 1).dispose();
                            });
                          },
                        );
                      },
                    ),
                  ),
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildControlPane() {
    return Padding(
      padding: const EdgeInsets.fromLTRB(20, 18, 20, 18),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Row(
            children: [
              Icon(Icons.power_settings_new, size: 21),
              SizedBox(width: 8),
              Text(
                '运行',
                style: TextStyle(fontSize: 18, fontWeight: FontWeight.w700),
              ),
            ],
          ),
          const SizedBox(height: 14),
          Row(
            children: [
              Expanded(
                child: OutlinedButton.icon(
                  onPressed: _writeConfigOnly,
                  icon: const Icon(Icons.description_outlined),
                  label: const Text('生成配置'),
                ),
              ),
              const SizedBox(width: 10),
              Expanded(
                child: OutlinedButton.icon(
                  onPressed: _fakePlcRunning ? _stopFakePlc : _startFakePlc,
                  icon: Icon(_fakePlcRunning ? Icons.stop : Icons.memory),
                  label: Text(_fakePlcRunning ? '停止假 PLC' : '启动假 PLC'),
                ),
              ),
            ],
          ),
          const SizedBox(height: 10),
          SizedBox(
            width: double.infinity,
            child: FilledButton.icon(
              onPressed: _gatewayRunning ? _stopGateway : _startGateway,
              icon: Icon(
                _gatewayRunning ? Icons.stop_circle_outlined : Icons.play_arrow,
              ),
              label: Text(_gatewayRunning ? '停止网关' : '启动网关'),
            ),
          ),
          const SizedBox(height: 10),
          SizedBox(
            width: double.infinity,
            child: OutlinedButton.icon(
              onPressed: _monitorRunning
                  ? () => _stopMonitorProcess()
                  : _startMonitor,
              icon: Icon(
                _monitorRunning ? Icons.visibility_off : Icons.visibility,
              ),
              label: Text(_monitorRunning ? '停止实时监控' : '启动实时监控'),
            ),
          ),
          const SizedBox(height: 16),
          _InfoLine(label: 'OPC UA 地址', value: _opcuaUrl()),
          _InfoLine(label: '配置文件', value: _lastConfigPath ?? '尚未生成'),
          const SizedBox(height: 16),
          Row(
            children: [
              const Text('日志', style: TextStyle(fontWeight: FontWeight.w700)),
              const Spacer(),
              IconButton(
                tooltip: '清空日志',
                onPressed: () => setState(_logs.clear),
                icon: const Icon(Icons.clear_all),
              ),
            ],
          ),
          Expanded(
            child: DecoratedBox(
              decoration: BoxDecoration(
                color: const Color(0xFF111513),
                borderRadius: BorderRadius.circular(8),
              ),
              child: ListView.builder(
                controller: _logScroll,
                padding: const EdgeInsets.all(12),
                itemCount: _logs.length,
                itemBuilder: (context, index) {
                  return SelectableText(
                    _logs[index],
                    style: const TextStyle(
                      color: Color(0xFFDDE6DF),
                      fontFamily: 'Menlo',
                      fontSize: 12,
                      height: 1.35,
                    ),
                  );
                },
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildLiveValuesPane() {
    final values = _liveValues.values.toList()
      ..sort((a, b) {
        final plc = a.plc.compareTo(b.plc);
        if (plc != 0) return plc;
        return a.tag.compareTo(b.tag);
      });

    return Padding(
      padding: const EdgeInsets.fromLTRB(20, 14, 20, 18),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(
                _monitorRunning
                    ? Icons.sensors_outlined
                    : Icons.sensors_off_outlined,
                size: 21,
              ),
              const SizedBox(width: 8),
              const Text(
                '实时值',
                style: TextStyle(fontSize: 18, fontWeight: FontWeight.w700),
              ),
              const Spacer(),
              _StatusChip(
                color: _monitorRunning
                    ? const Color(0xFF168A4A)
                    : const Color(0xFF777D78),
                label: _monitorRunning ? '监控中' : '未监控',
              ),
            ],
          ),
          const SizedBox(height: 10),
          Expanded(
            child: DecoratedBox(
              decoration: BoxDecoration(
                color: Colors.white,
                border: Border.all(color: const Color(0xFFD9DEDA)),
                borderRadius: BorderRadius.circular(8),
              ),
              child: ClipRRect(
                borderRadius: BorderRadius.circular(8),
                child: values.isEmpty
                    ? const Center(child: Text('生成配置或启动网关后显示实时值'))
                    : SingleChildScrollView(
                        scrollDirection: Axis.horizontal,
                        child: SizedBox(
                          width: 760,
                          child: ListView.separated(
                            itemCount: values.length + 1,
                            separatorBuilder: (context, index) =>
                                const Divider(height: 1),
                            itemBuilder: (context, index) {
                              if (index == 0) return const _LiveValueHeader();
                              return _LiveValueRow(value: values[index - 1]);
                            },
                          ),
                        ),
                      ),
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class LiveValue {
  const LiveValue({
    required this.plc,
    required this.tag,
    required this.node,
    required this.value,
    required this.quality,
    required this.ok,
    required this.updatedAt,
  });

  factory LiveValue.fromJson(Map<String, dynamic> json) {
    final ts = json['ts'];
    return LiveValue(
      plc: '${json['plc'] ?? ''}',
      tag: '${json['tag'] ?? ''}',
      node: '${json['node'] ?? ''}',
      value: '${json['value'] ?? '-'}',
      quality: '${json['quality'] ?? 'BAD'}',
      ok: json['ok'] == true,
      updatedAt: ts is int
          ? DateTime.fromMillisecondsSinceEpoch(ts * 1000)
          : DateTime.now(),
    );
  }

  final String plc;
  final String tag;
  final String node;
  final String value;
  final String quality;
  final bool ok;
  final DateTime? updatedAt;

  String get updatedText {
    final time = updatedAt;
    if (time == null) return '-';
    return time.toLocal().toIso8601String().substring(11, 19);
  }
}

class TagDraft {
  TagDraft({
    String initialName = '',
    this.area = 'DB',
    String initialDb = '1',
    String initialStart = '0',
    String initialBit = '',
    this.type = 'REAL',
  }) : name = TextEditingController(text: initialName),
       db = TextEditingController(text: initialDb),
       start = TextEditingController(text: initialStart),
       bit = TextEditingController(text: initialBit);

  final TextEditingController name;
  final TextEditingController db;
  final TextEditingController start;
  final TextEditingController bit;
  String area;
  String type;

  Map<String, dynamic> toJson() {
    final parsedStart = int.tryParse(start.text.trim());
    if (parsedStart == null) throw '${name.text} 的 start 必须是整数';

    final json = <String, dynamic>{
      'name': name.text.trim(),
      'area': area,
      'start': parsedStart,
      'type': type,
    };

    if (area == 'DB') {
      final parsedDb = int.tryParse(db.text.trim());
      if (parsedDb == null) throw '${name.text} 的 DB 号必须是整数';
      json['db'] = parsedDb;
    }

    if (type == 'BOOL') {
      final parsedBit = int.tryParse(bit.text.trim());
      if (parsedBit == null || parsedBit < 0 || parsedBit > 7) {
        throw '${name.text} 的 bit 必须是 0 到 7';
      }
      json['bit'] = parsedBit;
    }

    return json;
  }

  void dispose() {
    name.dispose();
    db.dispose();
    start.dispose();
    bit.dispose();
  }
}

class _Header extends StatelessWidget {
  const _Header({
    required this.gatewayRunning,
    required this.fakePlcRunning,
    required this.statusColor,
    required this.opcuaUrl,
  });

  final bool gatewayRunning;
  final bool fakePlcRunning;
  final Color statusColor;
  final String opcuaUrl;

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (context, constraints) {
        final compact = constraints.maxWidth < 900;
        return Container(
          height: compact ? 76 : 86,
          padding: const EdgeInsets.symmetric(horizontal: 24),
          decoration: const BoxDecoration(
            color: Color(0xFFEAEDEA),
            border: Border(bottom: BorderSide(color: Color(0xFFD2D8D3))),
          ),
          child: Row(
            children: [
              const Icon(Icons.hub_outlined, size: 30),
              const SizedBox(width: 12),
              Expanded(
                child: Column(
                  mainAxisAlignment: MainAxisAlignment.center,
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    const Text(
                      'S7ToOPCUA',
                      style: TextStyle(
                        fontSize: 24,
                        fontWeight: FontWeight.w800,
                      ),
                    ),
                    if (!compact)
                      const Text(
                        '西门子 S7 转 OPC UA 网关控制台',
                        style: TextStyle(color: Color(0xFF5C665F)),
                      ),
                  ],
                ),
              ),
              if (!compact) ...[
                _StatusChip(
                  color: fakePlcRunning
                      ? const Color(0xFF4B6EAF)
                      : const Color(0xFF777D78),
                  label: fakePlcRunning ? '假 PLC 运行中' : '假 PLC 未启动',
                ),
                const SizedBox(width: 10),
              ],
              _StatusChip(
                color: statusColor,
                label: gatewayRunning ? '网关运行中' : '网关未启动',
              ),
              if (!compact) ...[
                const SizedBox(width: 18),
                SelectableText(
                  opcuaUrl,
                  style: const TextStyle(fontFamily: 'Menlo', fontSize: 13),
                ),
              ],
            ],
          ),
        );
      },
    );
  }
}

class _StatusChip extends StatelessWidget {
  const _StatusChip({required this.color, required this.label});

  final Color color;
  final String label;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
      decoration: BoxDecoration(
        border: Border.all(color: color.withValues(alpha: 0.35)),
        borderRadius: BorderRadius.circular(20),
        color: color.withValues(alpha: 0.08),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(Icons.circle, size: 9, color: color),
          const SizedBox(width: 7),
          Text(
            label,
            style: TextStyle(color: color, fontWeight: FontWeight.w700),
          ),
        ],
      ),
    );
  }
}

class _SettingsPane extends StatelessWidget {
  const _SettingsPane({
    required this.gatewayRoot,
    required this.opcuaPort,
    required this.plcName,
    required this.plcIp,
    required this.plcPort,
    required this.rack,
    required this.slot,
    required this.pollMs,
    required this.onFakePreset,
  });

  final TextEditingController gatewayRoot;
  final TextEditingController opcuaPort;
  final TextEditingController plcName;
  final TextEditingController plcIp;
  final TextEditingController plcPort;
  final TextEditingController rack;
  final TextEditingController slot;
  final TextEditingController pollMs;
  final VoidCallback onFakePreset;

  @override
  Widget build(BuildContext context) {
    return SingleChildScrollView(
      padding: const EdgeInsets.fromLTRB(20, 18, 20, 18),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Row(
            children: [
              Icon(Icons.settings_ethernet, size: 21),
              SizedBox(width: 8),
              Text(
                '配置',
                style: TextStyle(fontSize: 18, fontWeight: FontWeight.w700),
              ),
            ],
          ),
          const SizedBox(height: 16),
          _LabeledField(label: '项目目录', controller: gatewayRoot),
          const SizedBox(height: 14),
          _LabeledField(label: 'OPC UA 端口', controller: opcuaPort),
          const SizedBox(height: 22),
          const _SectionLabel('PLC'),
          const SizedBox(height: 10),
          _LabeledField(label: '名称', controller: plcName),
          const SizedBox(height: 10),
          _LabeledField(label: 'IP 地址', controller: plcIp),
          const SizedBox(height: 10),
          Row(
            children: [
              Expanded(
                child: _LabeledField(label: '端口', controller: plcPort),
              ),
              const SizedBox(width: 10),
              Expanded(
                child: _LabeledField(label: '轮询 ms', controller: pollMs),
              ),
            ],
          ),
          const SizedBox(height: 10),
          Row(
            children: [
              Expanded(
                child: _LabeledField(label: 'Rack', controller: rack),
              ),
              const SizedBox(width: 10),
              Expanded(
                child: _LabeledField(label: 'Slot', controller: slot),
              ),
            ],
          ),
          const SizedBox(height: 18),
          SizedBox(
            width: double.infinity,
            child: OutlinedButton.icon(
              onPressed: onFakePreset,
              icon: const Icon(Icons.science_outlined),
              label: const Text('载入本机假 PLC 示例'),
            ),
          ),
        ],
      ),
    );
  }
}

class _TagHeader extends StatelessWidget {
  @override
  Widget build(BuildContext context) {
    const style = TextStyle(
      fontSize: 12,
      fontWeight: FontWeight.w700,
      color: Color(0xFF55615A),
    );
    return Container(
      height: 42,
      color: const Color(0xFFF0F3F0),
      padding: const EdgeInsets.symmetric(horizontal: 10),
      child: const Row(
        children: [
          SizedBox(width: 210, child: Text('名称', style: style)),
          SizedBox(width: 82, child: Text('区域', style: style)),
          SizedBox(width: 78, child: Text('DB', style: style)),
          SizedBox(width: 86, child: Text('Start', style: style)),
          SizedBox(width: 78, child: Text('Bit', style: style)),
          SizedBox(width: 120, child: Text('类型', style: style)),
          Spacer(),
          SizedBox(width: 42),
        ],
      ),
    );
  }
}

class _TagRow extends StatelessWidget {
  const _TagRow({
    required this.tag,
    required this.areas,
    required this.types,
    required this.onChanged,
    required this.onDelete,
  });

  final TagDraft tag;
  final List<String> areas;
  final List<String> types;
  final VoidCallback onChanged;
  final VoidCallback onDelete;

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      height: 58,
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 8),
        child: Row(
          children: [
            SizedBox(width: 210, child: _BareField(controller: tag.name)),
            const SizedBox(width: 8),
            SizedBox(
              width: 74,
              child: _SmallDropdown(
                value: tag.area,
                values: areas,
                onChanged: (value) {
                  tag.area = value;
                  onChanged();
                },
              ),
            ),
            const SizedBox(width: 8),
            SizedBox(
              width: 70,
              child: _BareField(controller: tag.db, enabled: tag.area == 'DB'),
            ),
            const SizedBox(width: 8),
            SizedBox(width: 78, child: _BareField(controller: tag.start)),
            const SizedBox(width: 8),
            SizedBox(
              width: 70,
              child: _BareField(
                controller: tag.bit,
                enabled: tag.type == 'BOOL',
              ),
            ),
            const SizedBox(width: 8),
            SizedBox(
              width: 112,
              child: _SmallDropdown(
                value: tag.type,
                values: types,
                onChanged: (value) {
                  tag.type = value;
                  onChanged();
                },
              ),
            ),
            const Spacer(),
            IconButton(
              tooltip: '删除',
              onPressed: onDelete,
              icon: const Icon(Icons.delete_outline),
            ),
          ],
        ),
      ),
    );
  }
}

class _LiveValueHeader extends StatelessWidget {
  const _LiveValueHeader();

  @override
  Widget build(BuildContext context) {
    const style = TextStyle(
      fontSize: 12,
      fontWeight: FontWeight.w700,
      color: Color(0xFF55615A),
    );
    return Container(
      height: 38,
      color: const Color(0xFFF0F3F0),
      padding: const EdgeInsets.symmetric(horizontal: 12),
      child: const Row(
        children: [
          SizedBox(width: 140, child: Text('PLC', style: style)),
          SizedBox(width: 210, child: Text('点名', style: style)),
          SizedBox(width: 130, child: Text('值', style: style)),
          SizedBox(width: 100, child: Text('质量', style: style)),
          SizedBox(width: 90, child: Text('更新时间', style: style)),
        ],
      ),
    );
  }
}

class _LiveValueRow extends StatelessWidget {
  const _LiveValueRow({required this.value});

  final LiveValue value;

  @override
  Widget build(BuildContext context) {
    final qualityColor = value.ok
        ? const Color(0xFF168A4A)
        : const Color(0xFF9A3B2F);
    return SizedBox(
      height: 42,
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 12),
        child: Row(
          children: [
            SizedBox(
              width: 140,
              child: Text(value.plc, overflow: TextOverflow.ellipsis),
            ),
            SizedBox(
              width: 210,
              child: Text(value.tag, overflow: TextOverflow.ellipsis),
            ),
            SizedBox(
              width: 130,
              child: SelectableText(
                value.value,
                style: const TextStyle(
                  fontFamily: 'Menlo',
                  fontWeight: FontWeight.w700,
                ),
              ),
            ),
            SizedBox(
              width: 100,
              child: Text(
                value.quality,
                style: TextStyle(
                  color: qualityColor,
                  fontWeight: FontWeight.w800,
                ),
              ),
            ),
            SizedBox(
              width: 90,
              child: Text(
                value.updatedText,
                style: const TextStyle(fontFamily: 'Menlo', fontSize: 12),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _BareField extends StatelessWidget {
  const _BareField({required this.controller, this.enabled = true});

  final TextEditingController controller;
  final bool enabled;

  @override
  Widget build(BuildContext context) {
    return TextField(
      controller: controller,
      enabled: enabled,
      style: const TextStyle(fontSize: 13),
      decoration: const InputDecoration(
        contentPadding: EdgeInsets.symmetric(horizontal: 9, vertical: 9),
      ),
    );
  }
}

class _SmallDropdown extends StatelessWidget {
  const _SmallDropdown({
    required this.value,
    required this.values,
    required this.onChanged,
  });

  final String value;
  final List<String> values;
  final ValueChanged<String> onChanged;

  @override
  Widget build(BuildContext context) {
    return DropdownButtonFormField<String>(
      initialValue: value,
      isExpanded: true,
      decoration: const InputDecoration(
        contentPadding: EdgeInsets.symmetric(horizontal: 9, vertical: 9),
      ),
      items: values
          .map((item) => DropdownMenuItem(value: item, child: Text(item)))
          .toList(),
      onChanged: (value) {
        if (value != null) onChanged(value);
      },
    );
  }
}

class _LabeledField extends StatelessWidget {
  const _LabeledField({required this.label, required this.controller});

  final String label;
  final TextEditingController controller;

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          label,
          style: const TextStyle(fontSize: 12, fontWeight: FontWeight.w700),
        ),
        const SizedBox(height: 5),
        TextField(controller: controller),
      ],
    );
  }
}

class _SectionLabel extends StatelessWidget {
  const _SectionLabel(this.text);

  final String text;

  @override
  Widget build(BuildContext context) {
    return Text(
      text,
      style: const TextStyle(
        color: Color(0xFF59645E),
        fontWeight: FontWeight.w800,
        letterSpacing: 0,
      ),
    );
  }
}

class _InfoLine extends StatelessWidget {
  const _InfoLine({required this.label, required this.value});

  final String label;
  final String value;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 8),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            label,
            style: const TextStyle(fontSize: 12, fontWeight: FontWeight.w700),
          ),
          const SizedBox(height: 3),
          SelectableText(
            value,
            style: const TextStyle(
              fontFamily: 'Menlo',
              fontSize: 12,
              color: Color(0xFF39423C),
            ),
          ),
        ],
      ),
    );
  }
}

String _findGatewayRoot() {
  final seen = <String>{};
  final candidates = <Directory>[];

  void addAncestors(Directory start) {
    var current = start.absolute;
    while (seen.add(current.path)) {
      candidates.add(current);
      final parent = current.parent;
      if (parent.path == current.path) break;
      current = parent;
    }
  }

  addAncestors(Directory.current);
  final executableDir = File(Platform.resolvedExecutable).parent;
  final bundledRuntime = _bundledRuntimeRoot(executableDir);
  if (bundledRuntime != null) {
    return bundledRuntime;
  }
  addAncestors(executableDir);

  for (final dir in candidates) {
    if (File(_join(dir.path, 'gateway')).existsSync() &&
        Directory(_join(dir.path, 'config')).existsSync()) {
      return dir.path;
    }
  }

  return Directory.current.path;
}

String? _bundledRuntimeRoot(Directory executableDir) {
  var current = executableDir.absolute;
  while (true) {
    if (current.uri.pathSegments.isNotEmpty &&
        current.path.endsWith('${Platform.pathSeparator}Contents')) {
      final runtime = Directory(
        _join(_join(current.path, 'Resources'), 'gateway_runtime'),
      );
      if (File(_join(runtime.path, 'gateway')).existsSync() &&
          Directory(_join(runtime.path, 'config')).existsSync()) {
        return runtime.path;
      }
      return null;
    }
    final parent = current.parent;
    if (parent.path == current.path) return null;
    current = parent;
  }
}

String _join(String left, String right) {
  if (left.endsWith(Platform.pathSeparator)) return '$left$right';
  return '$left${Platform.pathSeparator}$right';
}
