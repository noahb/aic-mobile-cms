<?php

/**
 * @file
 * Import functions for the AICAPP module.
 */

/**
 * Form callback.
 */
function aicapp_import_batch(&$form, &$form_state) {
  $counts = array();
  $form['import_data'] = array(
    '#type' => 'fieldset',
    '#collapsible' => TRUE,
    '#collapsed' => FALSE,
    '#title' => t('Import Audio Content'),
    '#tree' => FALSE,
    '#weight' => 10,
  );
  // Configure file names and directory locations.
  $form['import_data']['aicapp_import_files_directory'] = array(
    '#type' => 'textfield',
    '#size' => 255,
    '#maxlength' => 255,
    '#title' => t('Name of directory to import MP3 files from.'),
    '#default_value' => variable_get('aicapp_import_files_directory', 'audio'),
    '#description' => t('The path to the directory that contains the audio (mp3) files.'),
  );
  $form['import_data']['csv_file'] = array(
    '#title' => t('CSV File.'),
    '#type' => 'file',
    '#description' => t('Choose the CSV file to import.'),
  );
  $form['import_data']['upload'] = array(
    '#type' => 'submit',
    '#value' => 'Upload & Import',
    '#validate' => array('aicapp_import_batch_validate'),
    '#submit' => array('aicapp_import_batch_submit'),
  );
  $form['import_data']['aicapp_import_ops'] = array(
    '#type' => 'fieldset',
    '#collapsible' => TRUE,
    '#collapsed' => TRUE,
    '#title' => t('CSV Template'),
    '#tree' => FALSE,
  );
  $form['import_data']['aicapp_import_ops']['dowload_template'] = array(
    '#type' => 'submit',
    '#value' => t('Download CSV'),
    '#prefix' => '<div>',
    '#suffix' => t('Click this button to download a CSV template with all audio nodes pre-filled.') . '</div>',
    '#submit' => array('aicapp_import_download_submit'),
  );
  // Return even though passed by reference because the caller needs to return
  // the form.
  return $form;
}

/**
 * Admin setting validation callback.
 */
function aicapp_import_batch_validate($form, &$form_state) {
  $directory = variable_get('file_public_path', conf_path() . '/files');
  $values = $form_state['values'];
  $destination = file_prepare_directory($directory);
  if (!$destination) {
    form_set_error('csv_file', t('The public files directory could not be written to.'));
    return;
  }
  if (!file_exists(variable_get('file_public_path', conf_path() . '/files') . '/' . $values['aicapp_import_files_directory'])) {
    form_set_error('aicapp_import_files_directory', t('The files import directory was not readable.'));
  }
}

/**
 * Download CSV template submit callback.
 */
function aicapp_import_download_submit($form, &$form_state) {
  // Grab the csv template.
  $csv = aicapp_import_generate_template();
  if (empty($csv)) {
    return;
  }
  $delimiter = ',';
  $enclosure = '"';
  $filename = 'import-audio.csv';
  // Open a file to write, and use a generated name.
  $destination = drupal_tempnam('temp://', 'file');
  $file = fopen($destination, 'w');
  // Register a filter that writes "/n/r" line endings.
  stream_filter_register('crlf', 'CRLFilter');
  // Attach filter to output file.
  stream_filter_append($file, 'crlf');
  foreach ($csv as $row) {
    // Use putcsv to write to file.
    fputcsv($file, $row, $delimiter, $enclosure);
  }
  fclose($file);
  if ($data = file_get_contents($destination)) {
    $file = file_unmanaged_save_data($data, "public://" . $filename, FILE_EXISTS_REPLACE);
    $headers = array(
      'Content-Type' => 'application/octet-stream',
      'Content-Disposition' => 'attachment; filename="' . $filename . '"',
      'Content-Length' => filesize($file),
    );
    // Add headers to trigger download and file name.
    file_transfer($file, $headers);
  }
}

/**
 * Admin setting validation callback.
 */
function aicapp_import_batch_submit($form, &$form_state) {
  $directory = variable_get('file_public_path', conf_path() . '/files');
  $values = $form_state['values'];
  $destination = 'public://';
  $file = file_save_upload('csv_file', array('file_validate_extensions' => array('csv')), $destination, FILE_EXISTS_REPLACE);
  if (!is_object($file)) {
    drupal_set_message('There was a problem uploading the CSV file, please try again.');
    return;
  }
  else {
    $file->status = FILE_STATUS_PERMANENT;
    file_save($file);
    variable_set('aicapp_import_file', $file->uri);
  }
  $path = variable_get('aicapp_import_file', NULL);
  if (!file_exists($path)) {
    form_set_error('csv_file', t('The CSV file was not found.'));
    return;
  }
  elseif (!file_get_contents($path, NULL, NULL, 0, 10000)) {
    form_set_error('migration_fields_submit', t('The file was not readable.'));
    return;
  }
  // Check that files are available and readable.
  if ($items = aicapp_import_parse_csv($path)) {
    aicapp_import_items($items, $values['aicapp_import_files_directory']);
  }
  else {
    form_set_error('migration_fields_submit', t('The file was not readable.'));
  }
  // Save the values that were submitted.
  variable_set('aicapp_import_files_directory', $values['aicapp_import_files_directory']);
}

/**
 * Export a template csv file.
 */
function aicapp_import_generate_template() {
  $csv = array(array(
    'Object Selector #',
    'Object ID',
    'Node ID',
    'Title',
    'Track Title Eng',
    'filename_eng.mp3',
    'Transcripts Eng',
    'Credits Eng',
    'Track Title Esp',
    'filename_esp.mp3',
    'Transcripts Esp',
    'Credits Esp',
    'Track Title Chn',
    'filename_chn.mp3',
    'Transcripts Chn',
    'Credits Chn',
    )
  );
  $audio_files = node_load_multiple(array(), array('type' => AICAPP_TYPE_AUDIO));
  foreach ($audio_files as $nid => $node) {
    $row = array(
      NULL, NULL, $node->nid, $node->title,
    );
    // EN values.
    $node->language = 'en';
    // English Track Title
    $track_title = NULL;
    if (!empty($node->field_track_title[$node->language])) {
      $track_title = $node->field_track_title[$node->language][0]['value'];
    }
    $row[] = $track_title;
    // filename_eng.mp3
    $filename = NULL;
    if (!empty($node->field_audio_file[$node->language])) {
      $filename = $node->field_audio_file[$node->language][0]['filename'];
    }
    $row[] = $filename;
    // Transcripts Eng
    $transcripts = NULL;
    if (!empty($node->field_audio_transcript[$node->language])) {
      $transcripts = $node->field_audio_transcript[$node->language][0]['value'];
    }
    $row[] = $transcripts;
    // Credits Eng
    $credits = NULL;
    if (!empty($node->field_credits[$node->language])) {
      $credits = $node->field_credits[$node->language][0]['value'];
    }
    $row[] = $credits;
    // ES values.
    $node->language = 'es';
    // Track Title Esp
    $track_title = NULL;
    if (!empty($node->field_track_title[$node->language])) {
      $track_title = $node->field_track_title[$node->language][0]['value'];
    }
    $row[] = $track_title;
    // filename_esp.mp3
    $filename = NULL;
    if (!empty($node->field_audio_file[$node->language])) {
      $filename = $node->field_audio_file[$node->language][0]['filename'];
    }
    $row[] = $filename;
    // Transcripts Esp
    $transcripts = NULL;
    if (!empty($node->field_audio_transcript[$node->language])) {
      $transcripts = $node->field_audio_transcript[$node->language][0]['value'];
    }
    $row[] = $transcripts;
    // Credits Esp
    $credits = NULL;
    if (!empty($node->field_credits[$node->language])) {
      $credits = $node->field_credits[$node->language][0]['value'];
    }
    $row[] = $credits;
    // ZH / Chinese
    $node->language = 'zh-hant';
    // Track Title Chn
    $track_title = NULL;
    if (!empty($node->field_track_title[$node->language])) {
      $track_title = $node->field_track_title[$node->language][0]['value'];
    }
    $row[] = $track_title;
    // filename_chn.mp3
    $filename = NULL;
    if (!empty($node->field_audio_file[$node->language])) {
      $filename = $node->field_audio_file[$node->language][0]['filename'];
    }
    $row[] = $filename;
    // Transcripts Chn
    $transcripts = NULL;
    if (!empty($node->field_audio_transcript[$node->language])) {
      $transcripts = $node->field_audio_transcript[$node->language][0]['value'];
    }
    $row[] = $transcripts;
    // Credits Chn
    $credits = NULL;
    if (!empty($node->field_credits[$node->language])) {
      $credits = $node->field_credits[$node->language][0]['value'];
    }
    $row[] = $credits;
    // Add this row to the CSV array.
    $csv[] = $row;
  }
  return count($csv) > 1 ? $csv : FALSE;
}

/**
 * Parse a CSV file into an array.
 */
function aicapp_import_parse_csv($path, $delimiter = ',') {
  $header = NULL;
  $path = drupal_realpath($path);
  $data = array();
  ini_set('auto_detect_line_endings', TRUE);
  if (($handle = fopen($path, 'r')) !== FALSE) {
    while (($row = fgetcsv($handle, 0, $delimiter)) !== FALSE) {
      if (!$header) {
        $header = $row;
        $header_count = count($header);
      }
      elseif ($header_count === count($row)) {
        $data[] = array_combine($header, $row);
      }
      else {
        continue;
      }
    }
    fclose($handle);
  }
  return $data;
}

/**
 * Import an item
 */
function aicapp_import_items($items, $audio_file_path) {
  module_load_include('inc', 'entity', 'file_entity.file_api');
  $skip_text = array(
    'No transcript',
    'placeholder',
    'Placeholder transcript',
    'Placeholder transcript until we get this'
  );
  $counts = array(
    'audio' => array(
      'created' => 0,
      'updated' => 0,
      'associated' => 0,
      'ids' => array(),
      'duplicated' => array(),
    ),
  );
  try {
    // Read file for number of records.
    foreach ($items as $values) {
      $audio_node = NULL;
      if (!empty($values['Node ID'])) {
        $audio_node = node_load($values['Node ID']);
      }
      if (!empty($values['Title']) && !empty($values['Track Title Eng']) && !empty($values['filename_eng.mp3'])) {
        if (!is_object($audio_node)) {
          // Create a audio node entity.
          $default_values = array(
            'type' => AICAPP_TYPE_AUDIO,
            'uid' => 1,
            'status' => 1,
            'promote' => 0,
            'language' => 'en',
          );
          // Create the new audio node.
          $audio_node = entity_create('node', $default_values);
        }
        // Get the translation handler.
        $handler = entity_translation_get_handler('node', $audio_node);
        $entity = entity_metadata_wrapper('node', $audio_node);
        // Set the entity titlel and track title.
        $entity->title_field->set($values['Title']);
        $entity->language('en')->field_track_title->set($values['Track Title Eng']);
        // Look for the English file for this item.
        if ($file_obj = _aicapp_import_file_uri_to_object('public://' . $audio_file_path . '/' . $values['filename_eng.mp3'])) {
          $entity->language('en')->field_audio_file->file->set($file_obj);
        }
        // Set English transcripts and credits.
        if (!in_array($values['Transcripts Eng'], $skip_text)) {
          $entity->language('en')->field_audio_transcript->set($values['Transcripts Eng']);
        }
        if (!in_array($values['Credits Eng'], $skip_text)) {
          $entity->language('en')->field_credits->set($values['Credits Eng']);
        }
        // Check which languages to translate to.
        $translate_to = array();
        if (!empty($values['filename_esp.mp3']) && !empty($values['Track Title Esp'])) {
          $translate_to['es'] = 'es';
          $entity->language('es')->title_field->set($values['Title']);
          $entity->language('es')->field_track_title->set($values['Track Title Esp']);
          if (!in_array($values['Transcripts Esp'], $skip_text)) {
            $entity->language('es')->field_audio_transcript->set($values['Transcripts Esp']);
          }
          if (!in_array($values['Credits Esp'], $skip_text)) {
            $entity->language('es')->field_credits->set($values['Credits Esp']);
          }
          // Spanish audio file
          if ($file_obj = _aicapp_import_file_uri_to_object('public://' . $audio_file_path . '/' . $values['filename_esp.mp3'])) {
            $entity->language('es')->field_audio_file->file->set($file_obj);
          }
        }
        if (!empty($values['filename_chn.mp3']) && !empty($values['Track Title Chn'])) {
          $translate_to['zh-hant'] = 'zh-hant';
          $entity->language('zh-hant')->title_field->set($values['Title']);
          $entity->language('zh-hant')->field_track_title->set($values['Track Title Chn']);
          if (!in_array($values['Transcripts Chn'], $skip_text)) {
            $entity->language('zh-hant')->field_audio_transcript->set($values['Transcripts Chn']);
          }
          if (!in_array($values['Credits Chn'], $skip_text)) {
            $entity->language('zh-hant')->field_credits->set($values['Credits Chn']);
          }
          // Chinese audio file
          if ($file_obj = _aicapp_import_file_uri_to_object('public://' . $audio_file_path . '/' . $values['filename_chn.mp3'])) {
            $entity->language('zh-hant')->field_audio_file->file->set($file_obj);
          }
        }
        // For each translation to the English version of this node.
        foreach ($translate_to as $to_language) {
          $translation = array(
            'translate' => 0,
            'status' => 1,
            'language' => $to_language,
            'source' => 'en',
          );
          // Set and save translations
          $handler->setTranslation($translation, $entity->value());
          //$handler->saveTranslations();
        }
        // Save the audio node.
        $entity->save();

        $audio_id = $entity->nid->value();
        if ($audio_id) {
          // If created doesnt exactly match changed, assume update.
          if ($audio_node->created !== $audio_node->changed) {
            $counts['audio']['updated']++;
          }
          else {
            $counts['audio']['created']++;
          }
          $counts['audio']['ids'][$audio_id] = array(
            'nid' => $audio_id,
          );
        }
        // Now associate this audio node with the object that matches the
        // given object id.
        $query = new EntityFieldQuery();
        $query
          ->entityCondition('entity_type', 'node')
          ->propertyCondition('type', AICAPP_TYPE_OBJECT, '=')
          ->fieldCondition('field_object_id', 'value', $values['Object ID'], '=');
        $result = $query->execute();
        if (!empty($result['node'])) {
          $object_nids = array_keys($result['node']);
          $object_nodes = entity_load('node', $object_nids);
          if (count($object_nodes)) {
            $object_node = current($object_nodes);
            // Only add the assocaion if it doesn't already exist.
            // @TODO
            $current_items = array();
            if ($current = field_get_items('node', $object_node, 'field_audio_commentary')) {
              foreach ($current as $item) {
                $current_items[] = field_collection_field_get_entity($item);
              }
            }
            foreach ($current_items as $current_item) {
              $audio_item = !empty($current_item->field_audio_commentary_audio[LANGUAGE_NONE])
                        ? $current_item->field_audio_commentary_audio[LANGUAGE_NONE][0]['target_id'] : NULL;
              if ($audio_item === $audio_id) {
                // Audio node matches current, move to next audio item.
                $audio_associated = TRUE;
                break;
              }
            }
            if (empty($audio_associated)) {
              $object_entity = entity_metadata_wrapper('node', $object_node);
              $collection = entity_create('field_collection_item', array('field_name' => 'field_audio_commentary'));
              $collection->setHostEntity('node', $object_node);
              $collection_wrapper = entity_metadata_wrapper('field_collection_item', $collection);
              $item_values = array(
                'field_audio_commentary_audio' => array(
                  'value' => $audio_id,
                ),
              );
              if (!empty($values['Object Selector #'])) {
                $item_values['field_object_selector_number'] = array(
                  'value' => $values['Object Selector #'],
                );
              }
              foreach ($item_values as $key => $values) {
                $collection_wrapper->{$key}->set($values['value']);
              }
              $collection_wrapper->save();
              $object_entity->save();
              $counts['audio']['associated']++;
              $counts['audio']['ids'][$audio_id]['object'] = $object_entity->nid->value();
            }
          }
        }
      }
    }
  }
  catch (EntityMetadataWrapperException $e) {
    drupal_set_message(t('There was a problem importing audio.'), 'error');
  }
  catch (Exception $e) {
    $details = '';
    if (isset($e->errorInfo) && !empty($e->errorInfo[2])) {
      $details = $e->errorInfo[2];
    }
    drupal_set_message(t('There was a problem: %e', array('%e' => $details)), 'error');
  }
  if (!empty($counts['audio']['created'])) {
    drupal_set_message(format_plural($counts['audio']['created'], '1 new item has ', '@count new items have ') . t('been imported. See the !link.', array('!link' => l('New content items', 'admin/content'))));
  }
  if (!empty($counts['audio']['updated'])) {
    drupal_set_message(format_plural($counts['audio']['updated'], '1 existing item has ', '@count existing items have ') . t('been updated. See the !link.', array('!link' => l('Updated content items', 'admin/content'))));
  }
  if (!empty($counts['audio']['duplicated'])) {
    drupal_set_message(format_plural(count($counts['audio']['duplicated']), '1 duplicate item was ', '@count duplicate items were ') . t('found, and not imported.', array('!link' => l('New content items', 'admin/content'))));
    foreach ($counts['audio']['duplicated'] as $duplicate) {
      if (!empty($duplicate['found_nid'])) {
        drupal_set_message(t('An audio node with the title !link already exists.', array('!link' => l($duplicate['title'], 'node/' . $duplicate['found_nid']))));
      }
      elseif (!empty($duplicate['found_fid'])) {
        drupal_set_message(t('An file with the @filename already exists.', array('@filename' => $duplicate['filename'])));
      }
    }
  }
  if (empty($counts['audio']['duplicated']) && empty($counts['audio']['updated']) && empty($counts['audio']['created'])) {
    drupal_set_message(t('There were no records found to import.', array()));
  }
}

/**
 * Helper function to check if a audio node with the same title already exists.
 */
function _aicapp_import_title_exists($title) {
  // Check if a audio node with the given title exists.
  $query = new EntityFieldQuery();
  $query->entityCondition('entity_type', 'node')
    ->entityCondition('bundle', array(AICAPP_TYPE_AUDIO))
    ->propertyCondition('title', $title, '=')
    ->propertyCondition('status', 1);
  $result = $query->execute();
  if (!empty($result['node'])) {
    $nids = array_keys($result['node']);
    return current($nids);
  }
}

/**
 * Helper function to prepare and save file objects.
 */
function _aicapp_import_file_uri_to_object($uri, $use_existing = TRUE) {
  $file = FALSE;
  $uri = file_stream_wrapper_uri_normalize($uri);
  if ($use_existing) {
    // We should always attempt to re-use a file if possible.
    $files = entity_load('file', FALSE, array('uri' => $uri));
    $file = !empty($files) ? reset($files) : FALSE;
  }

  if (empty($file)) {
    $file = new stdClass();
    $file->uid = $GLOBALS['user']->uid;
    $file->filename = basename($uri);
    $file->uri = $uri;
    $file->filemime = file_get_mimetype($uri);
    // We can't use filesize() because that doesn't handle remote streams.
    $file_path = drupal_realpath($uri);
    $fp = fopen($file_path, 'r');
    if ($fp !== FALSE) {
      // Gather statistics and set the filesize;
      $fstat = fstat($fp);
      $file->filesize = $fstat['size'];
    }
    $file->timestamp = REQUEST_TIME;
    $file->status = FILE_STATUS_PERMANENT;
    // Repeat file_save actions
    module_invoke_all('file_presave', $file);
    module_invoke_all('entity_presave', $file, 'file');
    // Save the file to db.
    drupal_write_record('file_managed', $file);
    // Inform modules about the newly added file.
    module_invoke_all('file_insert', $file);
    module_invoke_all('entity_insert', $file, 'file');
    // Clear internal properties.
    unset($file->original);
    // Clear the static loading cache.
    entity_get_controller('file')->resetCache(array($file->fid));
  }

  return $file;
}
