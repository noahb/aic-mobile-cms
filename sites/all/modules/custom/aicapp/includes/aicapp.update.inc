<?php

/**
 * @file
 * Update functions for the AICAPP module.
 */


/**
 * Update the language of an entity.
 *
 */
function aicapp_update_content_language($types) {
  $nodes = _aic_get_nodes_without_language($types);
  if (!empty($nodes['node']) && aicapp_update_node_language($nodes['node'])) {
    return TRUE;
  }
}

/**
 * Update the fields of node values from on set of fields to another.
 *
 */
function aicapp_update_content_fields($types, $add_associations = 0) {
  $migration_attempted = $migration_success = 0;
  foreach ($types as $type) {
    if ($migration_attempted !== $migration_success) {
      return FALSE;
    }
    $migration_attempted++;
    $nodes = _aic_get_nodes_without_v2_field_values($type);

    if (!empty($nodes['node']) && aicapp_update_v2_field_values($nodes, $type, count($nodes['node'], $add_associations))) {
      $migration_success++;
    }
    elseif (empty($nodes)) {
      $migration_attempted--;
    }
  }
  if ($migration_attempted === $migration_success) {
    return TRUE;
  }
}

/**
 * Form callback.
 */
function aicapp_update_admin_migrate(&$form, &$form_state) {
  $all_migrations_complete = variable_get('aicapp_update_v1_v2', FALSE);
  // Data migration between v1 and v2.
  if (variable_get('aicapp_update_v1_v2_language', FALSE) && variable_get('aicapp_update_v1_v2_fields', FALSE)) {
    $all_migrations_complete = TRUE;
    $overview = t('All migrations complete');
  }
  global $language;
  $form['aicapp_data'] = array(
    '#type' => 'fieldset',
    '#collapsible' => TRUE,
    '#collapsed' => TRUE,
    '#title' => t('Content language and field migration'),
    '#tree' => TRUE,
    '#weight' => 10,
  );
  if ($all_migrations_complete) {
    $form['aicapp_data']['migration_remove_existing'] = array(
      '#type' => 'checkbox',
      '#title' => t('Remove Version 2 Field Values'),
      '#default_value' => (variable_get('migration_remove_existing', 0)) ?: 0,
      '#description' => t('Whether or not to remove all existing field_tour_stops & field_audio_commentary items.'),
    );
    $form['aicapp_data']['migration_repeat'] = array(
      '#prefix' => t('All data has been migrated.'),
      '#type' => 'submit',
      '#submit' => array('aicapp_update_admin_migrate_repeat_submit'),
      '#value' => t('Repeat Migration'),
    );
    return $form;
  }
  // Migrations left to do.
  $counts = $types = array(
    AICAPP_TYPE_AUDIO => t('Audio'),
    AICAPP_TYPE_TOUR => t('Tours'),
  );
  // Set default audio and tour counts.
  $counts[AICAPP_TYPE_TOUR] = 0;
  $counts[AICAPP_TYPE_AUDIO] = 0;
  $result = _aic_get_nodes_without_language(array_keys($types));
  $form['aicapp_data']['migration_language_prefix']['#markup'] = '<p><strong>' . t('Content Language Migration') . '</strong></p>';
  if (empty($result['node'])) {
    variable_set('aicapp_update_v1_v2_language', TRUE);
    $form['aicapp_data']['migration_language_complete'] = array(
      '#markup' => t('All content items have been updated to %lang.', array('%lang' => strtoupper($language->language))),
    );
  }
  else {
    $convert_types = '<p>' . t('The following types will be converted to @lang', array('@lang' => $language->name . ' (' . $language->language . ')')) . '</p><ul>';
    foreach ($types as $bundle => $type) {
      $convert_types .= '<li>' . $type . '</li>';
    }
    $form['aicapp_data']['migration_language_type_info'] = array(
      '#markup' => $convert_types . '</ul><br>',
    );
    $form['aicapp_data']['migration_language_submit'] = array(
      '#type' => 'submit',
      '#submit' => array('aicapp_update_admin_migrate_language_submit'),
      '#validate' => array('aicapp_update_admin_migrate_language_validate'),
      '#value' => t('Update Content Language'),
    );
    // Add results here for use in submit callback.
    $form_state['build_info']['args'] = array(
      0 => $result,
    );
  }
  $form['aicapp_data']['migration_fields_prefix']['#markup'] = '<p><strong>' . t('Content Fields Migration') . '</strong></p>';
  $types = array(
    AICAPP_TYPE_OBJECT => t('Objects'),
    AICAPP_TYPE_TOUR => t('Tours'),
  );
  // Set object and reset tour counts;
  $counts[AICAPP_TYPE_OBJECT] = 0;
  $counts[AICAPP_TYPE_TOUR] = 0;
  foreach ($types as $type => $label) {
    $result = _aic_get_nodes_without_v2_field_values($type);
    if (!empty($result['node'])) {
      // Validate that all the results can be migrated.
      $i = 0;
      foreach ($result['node'] as $nid => $obj) {
        if (empty($obj->type) || $obj->type !== $type) {
          continue;
        }
        $i++;
        $update_type = !empty($obj->update_type) ? $obj->update_type : 'fields';
        $callback = 'aicapp_update_validate_type_' . $type;
        $params = array($nid, &$counts, $i, &$to_add, $update_type);
        $valid = call_user_func_array($callback, $params);
        if (empty($valid)) {
          unset($result['node'][$nid]);
        }
      }
      // Add results here for use in submit callback.
      $form_state['build_info']['args'][$type] = $result;
      $counts[$type] = count($result['node']);
    }
  }
  if (empty($counts[AICAPP_TYPE_OBJECT]) && empty($counts[AICAPP_TYPE_TOUR])) {
    variable_set('aicapp_update_v1_v2_fields', TRUE);
    $form['aicapp_data']['migration_fields_complete'] = array(
      '#markup' => '<p>' . t('All content items fields have been updated.') . '</p>',
    );
  }
  else {
    if ($counts[AICAPP_TYPE_OBJECT] > 0) {
      $default = AICAPP_TYPE_OBJECT;
    }
    elseif ($counts[AICAPP_TYPE_TOUR] > 0) {
      $default = AICAPP_TYPE_TOUR;
    }
    if ($counts[AICAPP_TYPE_TOUR] === 0) {
      unset($types[AICAPP_TYPE_TOUR]);
    }
    if ($counts[AICAPP_TYPE_OBJECT] === 0) {
      unset($types[AICAPP_TYPE_OBJECT]);
    }
    // Only allow tour after all objects have been updated.
    $form['aicapp_data']['migration_fields_types'] = array(
      '#type' => 'checkboxes',
      '#title' => t('Select content type(s) with fields to be migrated:'),
      '#options' => $types,
      '#default_value' => array($default),
      '#description' => t('Every node of the chosen type(s) fields will be updated. Before updating tours, make sure that all objects have been updated.'),
    );
    // Disable the tour type if objects remain
    if ($counts[AICAPP_TYPE_OBJECT] > 0) {
      $form['aicapp_data']['migration_fields_types'][AICAPP_TYPE_TOUR]['#disabled'] = TRUE;
      $count = $counts[AICAPP_TYPE_OBJECT];
    }
    else {
      $count = $counts[AICAPP_TYPE_TOUR];
      $form['aicapp_data']['migration_add_associations'] = array(
        '#type' => 'checkbox',
        '#title' => t('Add Missing Audio Commentary to Objects'),
        '#default_value' => (variable_get('migration_add_associations', 1)) ?: 0,
        '#description' => t('When migrating tour stops, if a tour stop audio item is not already associated with the object for that stop, create the association.'),
      );
    }
    $form['aicapp_data']['migration_fields_batch'] = array(
      '#type' => 'numberfield',
      '#size' => 10,
      '#length' => 10,
      '#default_value' => $count,
      '#attributes' => array(
        'max' => max($counts[AICAPP_TYPE_OBJECT], $counts[AICAPP_TYPE_TOUR]),
      ),
      '#title' => t('Number of content items to update per batch.'),
    );
    $form['aicapp_data']['migration_fields_submit'] = array(
      '#type' => 'submit',
      '#submit' => array('aicapp_update_admin_migrate_field_submit'),
      '#value' => t('Update Content Fields'),
    );
  }
}

/**
 * Admin setting validation callback.
 */
function aicapp_update_admin_migrate_language_validate($form, &$form_state) {
  global $language;
  // What node type(s) are to be updated.
  if (empty($form_state['build_info']['args'])) {
    drupal_set_message('There were no items to migrate');
  }
}

/**
 * Admin setting validation callback.
 */
function aicapp_update_admin_migrate_repeat_submit($form, &$form_state) {
  variable_set('aicapp_update_v1_v2', FALSE);
  variable_set('aicapp_update_v1_v2_language', FALSE);
  variable_set('aicapp_update_v1_v2_fields', FALSE);
  if (empty($form_state['values']['aicapp_data']['migration_remove_existing'])) {
    return;
  }
  $fields = array(
    AICAPP_TYPE_TOUR => 'field_tour_stops',
    AICAPP_TYPE_OBJECT => 'field_audio_commentary',
  );
  // Remove all tour stop that were migrated allow for migration to happen again.
  aicapp_update_remove_field_values($fields);
}

/**
 * Admin setting validation callback.
 */
function aicapp_update_admin_migrate_language_submit($form, &$form_state) {
  $result = array_shift($form_state['build_info']['args']);
  if (aicapp_update_node_language($results['node'])) {
    $message = t('All @type content items with no language have been set to English', array('@type' => $type));
    drupal_set_message($message);
  }
}

/**
 * Admin setting validation callback.
 */
function aicapp_update_admin_migrate_field_submit($form, &$form_state) {
  $batch_size = !empty($form_state['values']['aicapp_data']['migration_fields_batch']) ? $form_state['values']['aicapp_data']['migration_fields_batch'] : 50;
  $add_associations = !empty($form_state['values']['aicapp_data']['migration_add_associations']) ? $form_state['values']['aicapp_data']['migration_add_associations'] : 0;
  $types = $form_state['values']['aicapp_data']['migration_fields_types'];
  foreach (array_filter($types) as $type) {
    if (empty($form_state['build_info']['args']) || empty($form_state['build_info']['args'][$type])) {
      continue;
    }
    aicapp_update_v2_field_values($form_state['build_info']['args'][$type], $type, $batch_size, $add_associations);
  }
}

/**
 * Update the language the site default for the passed array of nodes.
 */
function aicapp_update_node_language($nodes) {
  global $language;
  foreach ($nodes as $nid => $obj) {
    $node = node_load($nid);
    $field_languages = field_language('node', $node);
    foreach ($field_languages as $field => $field_language) {
      if ($field_language !== LANGUAGE_NONE) {
        continue;
      }
      if ($field === 'title_field') {
        $node->title_sync_source_lang = $language->language;
        $node->{$field}[$language->language] = array(
          0 => array('value' => $node->title_original),
        );
        unset($node->{$field}[LANGUAGE_NONE]);
      }
      else {
        $items = field_get_items('node', $node, $field, $field_language);
        if (!empty($items)) {
          $node->{$field}[$language->language] = $items;
        }
      }
    }
    if (!empty($node->{$field}[LANGUAGE_NONE])) {
      unset($node->{$field}[LANGUAGE_NONE]);
    }
    $node->language = $language->language;
    try {
      node_save($node);
    }
    catch (Exception $e) {
      return FALSE;
    }
  }
  return TRUE;
}

/**
 * Admin setting validation callback.
 */
function aicapp_update_v2_field_values($result, $type, $batch_size = 50, $add_associations = 0, $update_type = 'update', $update_msg = 'to use the new field format') {
  $counts = array();
  $counts[$type] = $i = 0;
  $to_add = array();
  if ($batch_size !== 0 && is_numeric($batch_size)) {
    $total = count($result['node']);
    // Limit to the $batch_size amount.
    $chunks = array_chunk($result['node'], $batch_size, TRUE);
  }
  else {
    $total = count($result['node']);
    $chunks = array($result['node']);
  }
  $result['node'] = array_shift($chunks);
  foreach ($result['node'] as $nid => $obj) {
    if (empty($obj->type) || $obj->type !== $type) {
      continue;
    }
    $i++;
    $update_type = !empty($obj->update_type) ? $obj->update_type : 'fields';
    $callback = 'aicapp_update_migrate_type_' . $type;
    $params = array(
      $nid,
      &$counts,
      $i,
      &$to_add,
      'update',
    );
    if ($obj->type === AICAPP_TYPE_TOUR) {
      $params[] = $add_associations;
    }
    elseif ($obj->type === AICAPP_TYPE_OBJECT) {
      $params[] = $update_type;
    }
    if (!call_user_func_array($callback, $params)) {
      return FALSE;
    }
  }
  $remaining = !empty($result['node']) ? count($result['node']) : 0;
  $type_message = array(
    '@type' => $type,
    '@update' => $update_msg,
    '@size' => $counts[$type],
    '@remain' => max(($total - $counts[$type]), 0),
  );
  drupal_set_message(t(
    '@size @type items have been migrated @update. @remain @type item(s) remain to migrate.',
    $type_message
  ));
  return TRUE;
}

/**
 * Remove the field values of the passed array of types and field names..
 *
 * $field_instances = array();
 */
function aicapp_update_remove_field_values($field_instances) {
  $types = array_keys($field_instances);
  // Remove the values for given fields.
  $query = new EntityFieldQuery();
  $query
    ->entityCondition('entity_type', 'node')
    ->entityCondition('bundle', $types, 'IN');
  $result = $query->execute();
  if (empty($result['node'])) {
    return;
  }
  $fields = array();
  foreach ($field_instances as $type => $field_name) {
    $fields[$type]['info'] = field_info_field($field_name);
    // $fields[$type]['instance'] = field_info_instance('node', $field_name, $type);
  }
  // Remove all values entered into the new field format.
  foreach ($result['node'] as $nid => $object) {
    $node = node_load($object->nid);
    $field_name = $field_instances[$node->type];
    if ($node->type === AICAPP_TYPE_OBJECT) {
      // Skip presave hooks here to avoid requests to data aggregator.
      $node->skip_hooks_presave = TRUE;
    }
    if (empty($node->{$field_name}[LANGUAGE_NONE])) {
      continue;
    }
    $field_info = $fields[$node->type]['info'];
    // $field_instance = $fields[$node->type]['instance'];
    $entity = entity_metadata_wrapper('node', $node);
    // Remove the field values.
    $field_values = array();
    foreach ($node->{$field_name}[LANGUAGE_NONE] as $key => $value) {
      // Build array of field collection values.
      $field_values[] = $value['value'];
      // Unset them.
      unset($node->{$field_name}[LANGUAGE_NONE][$key]);
    }
    // If this field is a field collection.
    if ($field_info['type'] === 'field_collection') {
      // Delete field collection items.
      entity_delete_multiple('field_collection_item', $field_values);
    }
    $entity->save();
  }
}

/**
 * Helper function to migrate tour fields.
 */
function aicapp_update_migrate_type_tour($nid, &$counts = array(), $i = 0, &$to_add = array(), $mode = 'update', $create_object_audio = FALSE) {
  if (empty($counts)) {
    $counts[AICAPP_TYPE_TOUR] = 0;
  }
  $node = node_load($nid);
  $node->_is_changed = FALSE;
  $entity_wrapper = entity_metadata_wrapper('node', $node);
  $found = array(
    'field_t_object' => NULL,
    'field_t_audio' => NULL,
    'field_t_audio_bumper' => NULL,
  );

  // Check for field_stops2 field collection items.
  if (!empty($node->field_stops2[LANGUAGE_NONE])) {
    $ids = array();
    foreach ($node->field_stops2[LANGUAGE_NONE] as $key => $value) {
      $ids[] = $value['value'];
    }
    $stops = field_collection_item_load_multiple($ids);
    $stops = current($stops);
    foreach ($found as $field => $n) {
      if (!isset($stops->{$field}[LANGUAGE_NONE])) {
        continue;
      }
      foreach ($stops->{$field}[LANGUAGE_NONE] as $id => $stop) {
        $found[$field][] = $stop['nid'];
      }
    }
  }
  // In terms of selector numbers, there are 3 cases.
  $count_object = count($found['field_t_object']);
  $count_audio = count($found['field_t_audio']);
  $count_bumper = count($found['field_t_audio_bumper']);
  // Check if there are currently audio commentary items.
  if (!empty($node->field_tour_stops[LANGUAGE_NONE])) {
    foreach ($node->field_tour_stops[LANGUAGE_NONE] as $key => $value) {
      $field_collection_item_values[] = $value['value'];
    }
  }
  // Add the new tour stop.
  if (!empty($found['field_t_object'])) {
    if ($mode === 'validate') {
      return TRUE;
    }
    // Before adding anything new, make sure these values are not already on
    // the node.
    $current_items = array();
    if ($current = field_get_items('node', $node, 'field_tour_stops')) {
      foreach ($current as $item) {
        $current_items[] = field_collection_field_get_entity($item);
      }
    }
    foreach ($found['field_t_object'] as $k => $object_id) {
      // If this object id exists in the current items, do not add it again.
      foreach ($current_items as $current_item) {
        $object_item = !empty($current_item->field_tour_stop_object[LANGUAGE_NONE])
                  ? $current_item->field_tour_stop_object[LANGUAGE_NONE][0]['target_id'] : NULL;
        if ($object_item === $object_id) {
          // Object node matches current, move to next tour stop item.
          continue 2;
        }
      }
      $item_values = array(
        'field_tour_stop_object' => array(
          'value' => $object_id,
        ),
      );
      if (isset($found['field_t_audio'][$k])) {
        $audio_nid = $found['field_t_audio'][$k];
        $object_node = node_load($object_id);
        if (!is_object($object_node)) {
          // This object doesn't exist, continue to next stop object.
          $replacement = array(
            '@tour' => $node->nid,
            '@object' => $object_id,
          );
          drupal_set_message(t(
            'There was a problem migrating tour stops for tour node ID: @tour. A stop contains an object node ID: @object, that does not exist.',
            $replacement
          ), 'warning');
          continue;
        }
        // Get all audio nids that are already associated with an object.
        $object_audio_nids = aicapp_update_get_object_audio_nids($object_node);
        $object_audio_found = array_key_exists($audio_nid, $object_audio_nids);
        // Check if the audio node was found in the object field_audio_commentary.
        if (!$object_audio_found && $create_object_audio) {
          $object_node->skip_hooks_presave = TRUE;
          $object_entity_wrapper = entity_metadata_wrapper('node', $object_node);
          // The audio node for this tour stop was not associated with the
          // object for this stop, so add it now.
          // Create the collection entity and set it's "host".
          $collection = entity_create('field_collection_item', array('field_name' => 'field_audio_commentary'));
          $collection->setHostEntity('node', $object_node);
          $collection_wrapper = entity_metadata_wrapper('field_collection_item', $collection);
          $collection_wrapper->field_audio_commentary_audio->set($audio_nid);
          $object_entity_wrapper->save();
          // Get the last item added.
          $all_items = $object_entity_wrapper->field_audio_commentary->value();
          $last = count($all_items) ? end($all_items) : NULL;
          if (isset($last->item_id)) {
            $item_values['field_tour_stop_audio_commentary'] = array(
              'value' => $last->item_id,
            );
          }
        }
        elseif ($object_audio_found) {
          // Audio NID was found as a key. The value of this key is the
          // field collection id, which is what we need to add to the $items_values.
          $item_values['field_tour_stop_audio_commentary'] = array(
            'value' => $object_audio_nids[$audio_nid],
          );
        }
      }
      if (isset($found['field_t_audio_bumper'][$k])) {
        $item_values['field_tour_stop_bumper'] = array(
          'value' => $found['field_t_audio_bumper'][$k],
        );
      }
      _aic_set_field_collection_items($node, 'field_tour_stops', $item_values);
    }
    $node->_is_changed = TRUE;
  }
  if ($node->_is_changed) {
    unset($node->_is_changed);
    $entity_wrapper->save();
    $counts[AICAPP_TYPE_TOUR]++;
  }
  return TRUE;
}

/**
 * Helper function to get all the audio nids associated with an object
 * via field_audio_commentary.
 */
function aicapp_update_get_object_audio_nids($object_node) {
  $item_values = array();
  if ($object_node && $field_items = field_get_items('node', $object_node, 'field_audio_commentary')) {
    foreach ($field_items as $audio_commentary_item) {
      $field_collection_entity = field_collection_field_get_entity($audio_commentary_item);
      $audio_items = field_get_items('field_collection_item', $field_collection_entity, 'field_audio_commentary_audio');
      if (empty($audio_items) || empty($audio_items[0]['target_id'])) {
        continue;
      }
      $item_values[$audio_items[0]['target_id']] = $field_collection_entity->item_id;
    }
  }
  return $item_values;
}

/**
 * Helper function to migrate tour fields.
 */
function aicapp_update_validate_type_tour($nid, &$counts = array(), $i = 0, &$to_add = array(), $type = 'fields') {
  $validate = aicapp_update_migrate_type_tour($nid, $counts, $i, $to_add, 'validate', $type);
  if ($validate) {
    return TRUE;
  }
}

/**
 * Helper function to migrate object fields.
 */
function aicapp_update_validate_type_object($nid, &$counts = array(), $i = 0, &$to_add = array(), $type = 'fields') {
  if (aicapp_update_migrate_type_object($nid, $counts, $i, $to_add, 'validate', $type)) {
    return TRUE;
  }
}


/**
 * Helper function to migrate object fields.
 */
function aicapp_update_migrate_type_object($nid, &$counts = array(), $i = 0, &$to_add = array(), $mode = 'update', $update_type = 'fields') {
  if (empty($counts)) {
    $counts[AICAPP_TYPE_OBJECT] = 0;
  }
  $node = node_load($nid);
  $node->_is_changed = FALSE;
  $node->skip_hooks_presave = TRUE;
  $entity_wrapper = entity_metadata_wrapper('node', $node);
  // If only validating, return the result of the object object function.
  if ($mode === 'validate') {
    if ($update_type === 'crop_fields') {
      return aicapp_update_migrate_type_object_update_crops($node, $i, $to_add, $mode);
    }
    return aicapp_update_migrate_type_object_update_audio($node, $i, $to_add, $mode);
  }
  if ($update_type === 'crop_fields') {
    // Call the crop function to modify by reference.
    aicapp_update_migrate_type_object_update_crops($node, $i, $to_add, $mode);
  }
  else {
    // Otherwise just call the function to modify by reference.
    aicapp_update_migrate_type_object_update_audio($node, $i, $to_add, $mode);
  }
  // If node is changed, save entity.
  if ($node->_is_changed) {
    unset($node->_is_changed);
    $entity_wrapper->save();
    $counts[AICAPP_TYPE_OBJECT]++;
  }
  return TRUE;
}

/**
 * Helper function to migrate object json fields.
 */
function aicapp_update_migrate_type_object_update_audio(&$node, $i, &$to_add, $mode = 'update') {
  $found = array(
    'field_object_selector_numbers' => NULL,
    'field_object_selector_number' => NULL,
    'field_object_audio' => NULL,
  );
  // Check for field_object_selector_numbers, field_object_selector_number, and field_object_audio
  if (!empty($node->field_object_selector_numbers[LANGUAGE_NONE])) {
    $found['field_object_selector_numbers'] = $node->field_object_selector_numbers[LANGUAGE_NONE];
  }
  if (!empty($node->field_object_selector_number[LANGUAGE_NONE])) {
    $found['field_object_selector_number'] = $node->field_object_selector_number[LANGUAGE_NONE];
  }
  if (!empty($node->field_object_audio[LANGUAGE_NONE])) {
    // Uses 'nid' as value key instead of 'value'
    $found['field_object_audio'] = $node->field_object_audio[LANGUAGE_NONE];
  }
  $count_multi = count($found['field_object_selector_numbers']);
  $count_audio = count($found['field_object_audio']);
  // In terms of selector numbers, there are 3 cases.
  if ($count_multi === 1) {
    // One of each, make sure they are the same.
    if ($found['field_object_selector_number'] &&
      $found['field_object_selector_number'][0]['value'] === $found['field_object_selector_numbers'][0]['value']) {
      // Numbers match, there is 1 matching selector being used.
      $to_add[$i]['selector'][] = $found['field_object_selector_number'][0]['value'];
      if ($count_audio) {
        foreach ($found['field_object_audio'] as $k => $v) {
          $to_add[$i]['audio'][] = $found['field_object_audio'][$k]['nid'];
        }
      }
      else {
        // No audio found unset this selector to add.
        unset($to_add[$i]['selector']);
      }
    }
    else {
      // Number will take precendence.
      $to_add[$i]['selector'][] = $found['field_object_selector_number'][0]['value'];
      $to_add[$i]['selector'][] = $found['field_object_selector_numbers'][LANGUAGE_NONE][0]['value'];
      // Multi and single do not match. Check audio count.
      if ($count_audio > 1) {
        foreach ($found['field_object_audio'] as $k => $v) {
          $to_add[$i]['audio'][] = $found['field_object_audio'][$k]['nid'];
        }

      }
      elseif ($count_audio === 1) {
        $to_add[$i]['audio'][] = $found['field_object_audio'][0]['nid'];
        unset($to_add[$i]['selector'][1]);
      }
      else {
        // No audio found unset this selector to add.
        unset($to_add[$i]['selector']);
      }
    }
  }
  // More than one field_object_selector_numbers found.
  elseif ($count_multi > 1) {
    foreach ($found['field_object_selector_numbers'] as $k => $v) {
      $to_add[$i]['selector'][] = $found['field_object_selector_numbers'][$k]['value'];
    }
    if ($count_audio) {
      foreach ($found['field_object_audio'] as $k => $v) {
        $to_add[$i]['audio'][] = $found['field_object_audio'][$k]['nid'];
      }
    }
    else {
      unset($to_add[$i]['selector']);
    }
  }
  // No multiple count but found one selector
  elseif ($found['field_object_selector_number']) {
    $to_add[$i]['selector'][] = $found['field_object_selector_number'][0]['value'];
    if ($count_audio) {
      foreach ($found['field_object_audio'] as $k => $v) {
        $to_add[$i]['audio'][] = $found['field_object_audio'][$k]['nid'];
      }
    }
  }
  elseif ($count_audio) {
    foreach ($found['field_object_audio'] as $k => $v) {
      $to_add[$i]['audio'][] = $found['field_object_audio'][$k]['nid'];
    }
  }
  // Add the new audio commentary
  if (!empty($to_add[$i]) && !empty($to_add[$i]['audio'])) {
    if ($mode === 'validate') {
      return TRUE;
    }
    // Before adding anything new, make sure these values are not already on
    // the node.
    $current_items = array();
    if ($current = field_get_items('node', $node, 'field_audio_commentary')) {
      foreach ($current as $item) {
        $current_items[] = field_collection_field_get_entity($item);
      }
    }
    foreach ($to_add[$i]['audio'] as $k => $audio_id) {
      // If this audio id exists in the current items, do not add it again.
      foreach ($current_items as $current_item) {
        $audio_item = !empty($current_item->field_audio_commentary_audio[LANGUAGE_NONE])
                  ? $current_item->field_audio_commentary_audio[LANGUAGE_NONE][0]['target_id'] : NULL;
        if ($audio_item === $audio_id) {
          // Audio node matches current, move to next audio item.
          continue 2;
        }
      }
      // Setup the values in the structure expected by the field_collection entity.
      $item_values = array(
        'field_audio_commentary_audio' => array(
          'value' => $audio_id,
        ),
      );
      if (isset($to_add[$i]['selector'][$k])) {
        $item_values['field_object_selector_number'] = array(
          'value' => $to_add[$i]['selector'][$k],
        );
      }
      _aic_set_field_collection_items($node, 'field_audio_commentary', $item_values);
    }
    $node->_is_changed = TRUE;
  }
}

/**
 * Helper function to migrate object crop fields.
 */
function aicapp_update_migrate_type_object_update_crops(&$node, $i, &$to_add, $mode = 'update') {
  $found = array(
    'field_large_image_crop_rect' => NULL,
    'field_thumbnail_crop_rect' => NULL,
  );
  // Check for field_large_image_crop_rect, and field_thumbnail_crop_rect
  if (!empty($node->field_large_image_crop_rect[LANGUAGE_NONE])) {
    $image_info = array(0, 0);
    if (!empty($node->field_large_image[LANGUAGE_NONE][0]['value'])) {
      $url = $node->field_large_image[LANGUAGE_NONE][0]['value'];
    }
    elseif (!empty($node->field_image[LANGUAGE_NONE][0]['uri'])) {
      $url = image_style_url('large_six_hundred', $node->field_image[LANGUAGE_NONE][0]['uri']);
    }
    if (!empty($url) && filter_var($url, FILTER_VALIDATE_URL) && $mode !== 'validate') {
      $image_info = getimagesize($url);
    }
    $id = $node->field_large_image_crop_rect[LANGUAGE_NONE][0];
    $crop = field_collection_item_load_multiple($id);
    $crop = current($crop);
    $to_add[$i]['crop_large'][] = array(
      'x1' => $crop->field_x[LANGUAGE_NONE][0]['value'],
      'y1' => $crop->field_y[LANGUAGE_NONE][0]['value'],
      'width' => $crop->field_width[LANGUAGE_NONE][0]['value'],
      'height' => $crop->field_height[LANGUAGE_NONE][0]['value'],
      'image_width' => $image_info[0],
      'image_height' => $image_info[1],
    );
  }
  if (!empty($node->field_thumbnail_crop_rect[LANGUAGE_NONE])) {
    $image_info = array(0, 0);
    if (!empty($node->field_thumbnail_image[LANGUAGE_NONE][0]['value'])) {
      $url = $node->field_thumbnail_image[LANGUAGE_NONE][0]['value'];
    }
    elseif (!empty($node->field_image[LANGUAGE_NONE][0]['uri'])) {
      $url = image_style_url('thumbnail_one_twelve', $node->field_image[LANGUAGE_NONE][0]['uri']);
    }
    if (!empty($url) && filter_var($url, FILTER_VALIDATE_URL) && $mode !== 'validate') {
      $image_info = getimagesize($url);
    }
    $id = $node->field_thumbnail_crop_rect[LANGUAGE_NONE][0];
    $crop = field_collection_item_load_multiple($id);
    $crop = current($crop);
    $to_add[$i]['crop_thumb'][] = array(
      'x1' => $crop->field_x[LANGUAGE_NONE][0]['value'],
      'y1' => $crop->field_y[LANGUAGE_NONE][0]['value'],
      'width' => $crop->field_width[LANGUAGE_NONE][0]['value'],
      'height' => $crop->field_height[LANGUAGE_NONE][0]['value'],
      'image_width' => $image_info[0],
      'image_height' => $image_info[1],
    );
  }
  // Add the new crops
  if (!empty($to_add[$i]) && (!empty($to_add[$i]['crop_thumb']) || !empty($to_add[$i]['crop_large']))) {
    if ($mode === 'validate') {
      return TRUE;
    }
    $crop_types = array();
    if (!empty($to_add[$i]['crop_large'])) {
      $crop_types['crop_large'] = 'field_large_image_crop';
    }
    if (!empty($to_add[$i]['crop_thumb'])) {
      $crop_types['crop_thumb'] = 'field_thumbnail_image_crop';
    }
    foreach ($crop_types as $type => $field_name) {
      foreach ($to_add[$i][$type] as $key => $crop_values) {
        // Setup the values in the structure expected by the field_collection entity.
        $item_values = array(
          'field_x' => array(
            'value' => $crop_values['x1'],
          ),
          'field_y' => array(
            'value' => $crop_values['y1'],
          ),
          'field_image_width' => array(
            'value' => $crop_values['image_width'],
          ),
          'field_image_height' => array(
            'value' => $crop_values['image_height'],
          ),
          'field_width' => array(
            'value' => $crop_values['width'],
          ),
          'field_height' => array(
            'value' => $crop_values['height'],
          ),
        );
        _aic_set_field_collection_items($node, $field_name, $item_values);
      }
    }
    $node->_is_changed = TRUE;
  }
}

/**
 * Find nodes of the passed type that do not have a language set.
 */
function _aic_set_field_collection_items(&$node, $field_name, $item_values) {
  // Create the collection entity and set it's "host".
  $collection = entity_create('field_collection_item', array('field_name' => $field_name));
  $collection->setHostEntity('node', $node);
  $collection_wrapper = entity_metadata_wrapper('field_collection_item', $collection);
  foreach ($item_values as $key => $values) {
    $collection_wrapper->{$key}->set($values['value']);
  }
}

/**
 * Find nodes of the passed type that do not have a language set.
 */
function _aic_get_nodes_without_language($types) {
  $query = new EntityFieldQuery();
  $query
    ->entityCondition('entity_type', 'node')
    ->entityCondition('bundle', $types, 'IN')
    ->propertyCondition('language', LANGUAGE_NONE, '=');
  return $query->execute();
}

/**
 * Find nodes of the passed type that do not have a language set.
 */
function _aic_get_nodes_without_v2_field_values($type, $query_tag = NULL) {
  $query = new EntityFieldQuery();
  $query
    ->entityCondition('entity_type', 'node')
    ->entityCondition('bundle', $type, '=');
  if (!empty($query_tag)) {
    $query->addTag($query_tag);
  }
  elseif ($type === AICAPP_TYPE_OBJECT) {
    $query->addTag('object_audio_missing');
  }
  elseif ($type === AICAPP_TYPE_TOUR) {
    $query->addTag('tour_stops_missing');
  }
  $result = $query->execute();
  if (!empty($result['node'])) {
    foreach ($result['node'] as $nid => $item) {
      $node = node_load($nid);
      if ($type === AICAPP_TYPE_OBJECT) {
        if (empty($query_tag) && empty($node->field_object_audio[LANGUAGE_NONE])) {
          // Exclude v1 object unless it has at least 1 audio node.
          unset($result['node'][$node->nid]);
        }
        elseif ($query_tag === 'thumb_crop_missing' && empty($node->field_thumbnail_crop_rect[LANGUAGE_NONE])) {
          // Exclude v1 object unless it has crop values.
          unset($result['node'][$node->nid]);
        }
        elseif ($query_tag === 'large_crop_missing' && empty($node->field_large_image_crop_rect[LANGUAGE_NONE])) {
          // Exclude v1 object unless it has crop values.
          unset($result['node'][$node->nid]);
        }
      }
      elseif ($type === AICAPP_TYPE_TOUR) {
        // Exclude v1 tour stops with an missing object reference.
        $found = array(
          'field_t_object' => NULL,
        );
        // Check for field_stops2 field collection items.
        if (!empty($node->field_stops2[LANGUAGE_NONE])) {
          $ids = array();
          foreach ($node->field_stops2[LANGUAGE_NONE] as $key => $value) {
            $ids[] = $value['value'];
          }
          $stops = field_collection_item_load_multiple($ids);
          $stops = current($stops);
          foreach ($found as $field => $n) {
            if (!isset($stops->{$field}[LANGUAGE_NONE])) {
              continue;
            }
            foreach ($stops->{$field}[LANGUAGE_NONE] as $id => $stop) {
              if (!node_load($stop['nid'])) {
                if (function_exists('dpm')) {
                  dpm('node ' . $node->nid . ' tour_stops2 field has a reference to an object nid of: ' . $stop['nid'] . ', but that node does not exist. ');
                }
                unset($result['node'][$node->nid]);
              }
            }
          }
        }
      }
    }
  }
  if ($type === AICAPP_TYPE_OBJECT && empty($query_tag)) {
    // Also check that crop values have been updated.
    $result = array_merge($result, aicapp_get_crop_field_objects());
  }
  return $result;
}

/**
 * Return object nodes with crop values needing to be copied.
 */
function aicapp_get_crop_field_objects() {
  $nodes = _aic_get_nodes_without_v2_field_values(AICAPP_TYPE_OBJECT, 'thumb_crop_missing');
  $nodes = array_merge($nodes, _aic_get_nodes_without_v2_field_values(AICAPP_TYPE_OBJECT, 'large_crop_missing'));
  // Mark the nodes as need crop updates.
  if (empty($nodes)) {
    return array();
  }
  foreach ($nodes['node'] as $nid => $node) {
    $nodes['node'][$nid]->update_type = 'crop_fields';
  }
  return $nodes;
}
