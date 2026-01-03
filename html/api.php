<?php
function call_mycelia($action, $payload) {
    // Ruft den Python Proxy auf Port 9999 auf
    $url = 'http://127.0.0.1:9999';
    
    $data = array('action' => $action);
    if ($action === 'encrypt') {
        $data['data'] = $payload;
    } else {
        $data['seed'] = $payload['seed'];
        $data['blob'] = $payload['blob'];
    }

    $options = array(
        'http' => array(
            'header'  => "Content-type: application/json\r\n",
            'method'  => 'POST',
            'content' => json_encode($data)
        )
    );
    $context  = stream_context_create($options);
    
    // Fehler unterdrücken (@), damit wir eine saubere Meldung ausgeben können
    $result = @file_get_contents($url, false, $context);
    
    if ($result === FALSE) { 
        die("<div style='background:red;color:white;padding:20px;font-family:monospace;'>" .
            "FEHLER: Der Mycelia Python-Proxy läuft nicht!<br>" .
            "Bitte starte <b>python db_proxy.py</b> in einer Konsole." .
            "</div>"); 
    }
    
    return json_decode($result, true);
}
?>