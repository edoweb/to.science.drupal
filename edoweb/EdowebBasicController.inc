<?php
/**
 * Copyright 2013 hbz NRW (http://www.hbz-nrw.de/)
 *
 * This file is part of regal-drupal.
 *
 * regal-drupal is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * regal-drupal is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with regal-drupal.  If not, see <http://www.gnu.org/licenses/>.
 */

class EdowebBasicController extends EntityAPIController {

  private $__entity_loaded = array();

  /**
   * 
   * Generate v4 UUID
   * 
   * Version 4 UUIDs are pseudo-random.
   */
  public static function uuid_v4() {
    return sprintf('%04x%04x-%04x-%04x-%04x-%04x%04x%04x',
      mt_rand(0, 0xffff), mt_rand(0, 0xffff),
      mt_rand(0, 0xffff),
      mt_rand(0, 0x0fff) | 0x4000,
      mt_rand(0, 0x3fff) | 0x8000,
      mt_rand(0, 0xffff), mt_rand(0, 0xffff), mt_rand(0, 0xffff)
      );
  }

  /**
   * Load an edoweb_basic entity.
   */
  public function load($remote_ids = array(), $conditions = array()) {
    $local_ids = array();
    foreach ($remote_ids as $remote_id) {
      $local_id = $this->_get_local_id($remote_id);
      if (!$local_id) {
        $api = new EdowebAPIClient();
        if ($api->importEntity($remote_id)) {
          $local_id = $this->_get_local_id($remote_id);
        }
      }
      if ($local_id && (1 == variable_get('edoweb_api_nocache'))
          && !isset($this->__entity_loaded[$local_id])) {
        cache_clear_all("field:edoweb_basic:{$local_id}", 'cache_field');
        $this->__entity_loaded[$local_id] = true;
        $local_ids[] = $local_id;
      } else if ($local_id) {
        $local_ids[] = $local_id;
      }
    }
    LibRDF_LiteralNode::setPlainOutput(true);
    $entities = parent::load($local_ids, $conditions);
    return $entities;
  }

  protected function _get_local_id($remote_id) {
    return db_query(
      'SELECT local_id FROM {edoweb_basic} WHERE remote_id = :remote_id',
      array(':remote_id' => $remote_id)
    )->fetchField();
  }

  public function clearCache($remote_id) {
    if ($local_id = $this->_get_local_id($remote_id)) {
      cache_clear_all("field:edoweb_basic:{$local_id}", 'cache_field');
    }
  }

  public function createFromRdf($subject_uri, $rdf_model) {
    // FIXME: Properly handle namespaces at loading level
    $remote_id = _edoweb_compact_uri($subject_uri);
    $bundle_type = _edoweb_bundle_type_from_rdf($remote_id, $rdf_model);
    $entity = parent::create(
      array(
        'remote_id' => $remote_id,
        'bundle_type' => $bundle_type,
      )
    );
    $rdf_parser = new LibRDF_Parser('rdfxml');
    _edoweb_storage_entity_deserialize_rdf(
      $entity, "$rdf_model", $rdf_parser
    );
    return $entity;
  }

  /**
   * Saves the custom fields using drupal_write_record()
   * Storage of attached fields is handled by a custom
   * field storage (module edoweb_storage).
   */
  public function save($entity) {
    // If our entity has no local_id, then we need to give it a
    // time of creation.
    if (empty($entity->local_id)) {
      $entity->created = time();
    }
    // If our entity has no remote_id, create one.
    if (empty($entity->remote_id)) {
      $namespace = variable_get('edoweb_api_namespace');
      $entity->remote_id = "$namespace:" . self::uuid_v4();
    }
    // If our entity has no owner, set current user.
    if (empty($entity->uid)) {
      global $user;
      $entity->uid = $user->uid;
    }
    // Invoke hook_entity_presave().
    module_invoke_all('entity_presave', $entity, 'edoweb_basic');
    // The 'primary_keys' argument determines whether this will be an insert
    // or an update. So if the entity already has an ID, we'll specify
    // local_id as the key.
    $primary_keys = $entity->internalIdentifier() ? 'local_id' : array();
    // Write out the entity record.
    try {
      drupal_write_record('edoweb_basic', $entity, $primary_keys);
    } catch (PDOException $e) {
      drupal_set_message(
        "Failed to write record for {$entity->remote_id}.", 'error'
      );
    }
    // We're going to invoke either hook_entity_update() or
    // hook_entity_insert(), depending on whether or not this is a
    // new entity. We'll just store the name of hook_entity_insert()
    // and change it if we need to.
    $invocation = 'entity_insert';
    // Now we need to either insert or update the fields which are
    // attached to this entity. We use the same primary_keys logic
    // to determine whether to update or insert, and which hook we
    // need to invoke.
    if (empty($primary_keys)) {
      field_attach_insert('edoweb_basic', $entity);
    }
    else {
      field_attach_update('edoweb_basic', $entity);
      $invocation = 'entity_update';
    }
    // Invoke either hook_entity_update() or hook_entity_insert().
    module_invoke_all($invocation, $entity, 'edoweb_basic');
    return $entity;
  }

  /**
   * Delete a single entity.
   *
   * Really a convenience function for delete_multiple().
   */
  public function delete($entity) {
    $this->delete_multiple(array($entity));
  }

  /**
   * Delete one or more edoweb_basic entities.
   *
   * @param $local_ids
   *   An array of entity IDs or a single numeric ID.
   */
  public function delete_multiple($entities) {
    $local_ids = array();
    if (!empty($entities)) {
      $transaction = db_transaction();
      try {
        foreach ($entities as $entity) {
          $children = field_get_items('edoweb_basic', $entity, 'field_edoweb_struct_child');
          if (FALSE !== $children) {
            foreach($children as $child) {
              $child_entity = edoweb_basic_load($child['value']);
              $this->delete($child_entity);
            }
          }
          // Invoke hook_entity_delete().
          module_invoke_all('entity_delete', $entity, 'edoweb_basic');
          field_attach_delete('edoweb_basic', $entity);
          $local_ids[] = $entity->local_id;
        }
        db_delete('edoweb_basic')
          ->condition('local_id', $local_ids, 'IN')
          ->execute();
      } catch (Exception $e) {
        $transaction->rollback();
        watchdog_exception('edoweb', $e);
        throw $e;
      }
    }
  }

}

